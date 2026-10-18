"""Builds assets/hulls (catalog.json + .nlmesh + PNG) from a Unity bundle and
verifies the result against the acceptance thresholds.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import struct
import time

from . import addressables, hierarchy, mathutil, meshes, serialized, textures, unityfs

EXTRACTOR_VERSION = "0.2.0"
BUNDLE_NAME = "stock-f2"

#: hull saveKey -> (role, container path, display name, catalog prefab path)
HULLS = {
    "Stock/Bulk Hauler": (
        "marauder",
        "assets/#bundles/stock-f2/civ/hulls/bulk hauler.prefab",
        "Bulk Hauler",
        "Assets/#BUNDLES/Stock-F2/Civ/Hulls/Bulk Hauler.prefab"),
    "Stock/Container Hauler": (
        "moorline",
        "assets/#bundles/stock-f2/f2/hulls/container line ship.prefab",
        "Container Hauler",
        "Assets/#BUNDLES/Stock-F2/F2/Hulls/Container Line Ship.prefab"),
    "Stock/Container Hauler Refit": (
        "moorline-refit",
        "assets/#bundles/stock-f2/f2/hulls/container line ship refit.prefab",
        "Container Hauler Refit",
        "Assets/#BUNDLES/Stock-F2/F2/Hulls/Container Line Ship Refit.prefab"),
}

IDENTITY_NODE = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))

#: Materials that are pure functional props (emitters, glazing) and never take
#: the faction livery.  Only consulted when the hull segment components cannot be
#: parsed (see livery_materials); otherwise livery membership comes from the game
#: data itself, because a hull piece referencing a material does not mean the
#: game recolours it (doodads, cranes and side tanks keep a copper tint).
FUNCTIONAL_MATERIALS = ("engine glow", "window", "light", "lamp", "flare",
                        "emissive", "thruster glow", "fx")

#: texture slot -> (material property, addressable asset name suffix)
AA_SLOTS = (("albedo", "_BaseColorMap", "_base_color"),
            ("mask", "_PaintMask", "_paint_mask"),
            ("normal", "_NormalMap", "_normal"),
            ("decal", "_nameplateDecal", "_nameplate_decal"))
AA_PROPERTY = {slot[0]: slot[1] for slot in AA_SLOTS}
AA_SUFFIX = {slot[0]: slot[2] for slot in AA_SLOTS}
#: further property names a slot accepts, tried after AA_PROPERTY[slot].  The
#: nameplate decal is authored as _nameplateDecal on HDRP decal materials and
#: as _DecalMap on older ones.
SLOT_ALIASES = {"decal": ("_DecalMap",)}
SLOT_NAMES = tuple(slot[0] for slot in AA_SLOTS)


def slot_properties(kind):
    """Material properties that map to one texture slot, primary first."""
    return (AA_PROPERTY[kind],) + tuple(SLOT_ALIASES.get(kind, ()))

#: hull body material name -> addressable "<Variant> Textures" folder.  The
#: authoring tool uses both "Marauder_A_Bow" and "CivBulk - A - Bow".
SEGMENT_MATERIALS = (
    re.compile(r"^(?P<ship>[A-Za-z0-9]+)_(?P<variant>[ABC])_(?P<segment>Bow|Mid|Core|Stern)"
               r"(?P<tail>_[A-Za-z0-9]+)?$"),
    re.compile(r"^(?P<ship>[A-Za-z0-9 ]+?) - (?P<variant>[ABC]) - "
               r"(?P<segment>Bow|Mid|Core|Stern)(?: (?P<tail>[A-Za-z]+))?$"),
)
#: the core segments are named "Mid" in the prefabs and "core" in the textures
SEGMENT_ALIAS = {"mid": "core"}

#: MonoBehaviour that stores a hull segment material's addressable GUIDs
SEGMENT_COMPONENT = "HullSegmentMultiMaterial"
CLASS_MATERIAL = 21
CLASS_TEXTURE = 28

DEFAULT_SIZES = {"albedo": 512, "mask": 512, "normal": 512, "decal": 256}


def is_functional_material(name):
    lowered = (name or "").lower()
    return any(pattern in lowered for pattern in FUNCTIONAL_MATERIALS)


#: Fields on a segment component naming the material the game clones before
#: recolouring it: the tint pass only affects the clone.
LIVERY_MATERIAL_FIELDS = ("_originalMaterial", "_originalMaterials")
#: The material property the tint pass writes; a material that does not declare it
#: (plain HDRP/Lit thruster and engine glow slots) keeps its authored colour.
PAINT_TINT_PROPERTY = "_BasePaintTint"
#: The map that paint tint is masked through.  A secondary structure handed to
#: The tint pass without one keeps its authored colour (the Bulk Hauler's bridge
#: shell is the container liner's mesh with an unpainted freighter material).
PAINT_MASK_PROPERTY = AA_PROPERTY["mask"]
#: Renderer classes an _additionalMaterialTargets entry may reference.
RENDERER_CLASSES = (hierarchy.CLASS_MESH_RENDERER, 137)


def _pptr_or_list(value):
    """pathIDs of a serialized field that holds a single PPtr or a PPtr array."""
    if isinstance(value, dict):
        path_id = hierarchy.pptr_path_id(value)
        return [path_id] if path_id else []
    return hierarchy.pptr_list(value)


def _renderer_materials(graph, path_id):
    """Material pathIDs of a PPtr<Renderer> or PPtr<GameObject> target."""
    info = graph.sf.by_path.get(path_id)
    if info is None:
        return []
    if info.class_id in RENDERER_CLASSES:
        renderers = [path_id]
    elif info.class_id == hierarchy.CLASS_GAMEOBJECT:
        renderers = [renderer for class_id in RENDERER_CLASSES
                     for renderer in graph.components_by_class_id(path_id, class_id)]
    else:
        return []
    found = []
    for renderer in renderers:
        found.extend(hierarchy.pptr_list(graph.value(renderer).get("m_Materials")))
    return found


def paintable_segments(graph, section):
    """PathIDs of the segments the game recolours for one hull section.

    The paintable-mesh field is a public array on the primary structures and the
    single _hullSeg of a secondary structure; the primary structure concatenates
    them.
    """
    value = graph.value(section.component)
    segments = hierarchy.pptr_list(value.get("PaintableMeshes"))
    if not segments:
        segments = _pptr_or_list(value.get("_hullSeg"))
    return segments


_SHADER_PROPERTY_CACHE = {}


def shader_properties(graph, shader_path):
    """Property names the shader actually declares, cached by shader pathID."""
    if shader_path not in _SHADER_PROPERTY_CACHE:
        names = set()
        parsed = (graph.value(shader_path) or {}).get("m_ParsedForm") or {}
        for prop in (parsed.get("m_PropInfo") or {}).get("m_Props") or ():
            if isinstance(prop, dict):
                name = prop.get("m_Name") or prop.get("first")
                if name:
                    names.add(name)
        _SHADER_PROPERTY_CACHE[shader_path] = names
    return _SHADER_PROPERTY_CACHE[shader_path]


def declares_paint_tint(graph, path_id, require_shader=False):
    """True when the material declares the property the tint pass writes.

    A material whose shader was swapped keeps the old tint values in m_Colors as dead
    properties (Hull_Shader_NoPaint has no _BasePaintTint at all), so a strict check also
    asks the shader. That is what tells the freighter bridge apart from the lineship one.
    """
    properties = graph.value(path_id).get("m_SavedProperties") or {}
    declared = any(isinstance(entry, dict) and entry.get("first") == PAINT_TINT_PROPERTY
                   for entry in properties.get("m_Colors") or ())
    if not declared:
        return False
    if not require_shader:
        return True
    shader = hierarchy.pptr_path_id(graph.value(path_id).get("m_Shader"))
    if not shader:
        return True
    return PAINT_TINT_PROPERTY in shader_properties(graph, shader)


def declares_paint_mask(graph, path_id):
    """True when the material carries the map the tint is applied through."""
    properties = graph.value(path_id).get("m_SavedProperties") or {}
    for entry in properties.get("m_TexEnvs") or ():
        if isinstance(entry, dict) and entry.get("first") == PAINT_MASK_PROPERTY:
            return bool(hierarchy.pptr_path_id(
                (entry.get("second") or {}).get("m_Texture")))
    return False


def segment_materials(graph, segment, require_shader=False):
    """Material pathIDs one segment component puts under the faction livery.

    Returns (materials, slots skipped for missing the paint tint property).
    """
    value = graph.value(segment)
    found = []
    for field in LIVERY_MATERIAL_FIELDS:
        found.extend(_pptr_or_list(value.get(field)))
    for target in hierarchy.pptr_list(value.get("_additionalMaterialTargets")):
        found.extend(_renderer_materials(graph, target))
    materials = []
    skipped = []
    for path_id in found:
        info = graph.sf.by_path.get(path_id)
        if info is None or info.class_id != CLASS_MATERIAL:
            continue
        if declares_paint_tint(graph, path_id, require_shader):
            materials.append(path_id)
        else:
            skipped.append(path_id)
    return materials, skipped


def livery_materials(graph, hulls):
    """material pathID -> hull saveKeys whose livery the material carries.

    The bake step assigns a clone of _originalMaterial (the _originalMaterials
    array when a segment uses more than one) to the segment's own renderers and
    to _additionalMaterialTargets, and the tint pass affects only that
    clone.  Those materials are therefore the ones the game really recolours;
    everything else keeps its authored colour.

    A secondary structure is repainted through its own _PaintMask: the three
    stock liners share the same four superstructure meshes, and the Bulk Hauler
    binds a freighter material authored without that map, which the game leaves
    unpainted (see declares_paint_mask).

    Returns (materials, resolved segments, unresolved segment labels, slot
    materials skipped because they do not declare PAINT_TINT_PROPERTY,
    superstructure materials skipped because they carry no _PaintMask).
    """
    materials = {}
    resolved = 0
    unresolved = []
    skipped = set()
    unmasked = set()
    for save_key, hull in hulls.items():
        for group in ("bows", "cores", "sterns", "superstructures"):
            for section in getattr(hull, group):
                for segment in paintable_segments(graph, section):
                    found, without_tint = segment_materials(
                        graph, segment, require_shader=(section.kind == "superstructure"))
                    skipped.update(without_tint)
                    if not found:
                        unresolved.append("%s %s" % (save_key, section.id))
                        continue
                    resolved += 1
                    for path_id in found:
                        # A material without _PaintMask still follows the tint pass (_BasePaintTint is
                        # what the game writes); it just cannot split base and trim colours.
                        if not declares_paint_mask(graph, path_id):
                            unmasked.add(path_id)
                        materials.setdefault(path_id, set()).add(save_key)
    return materials, resolved, unresolved, skipped, unmasked


class Logger(object):
    def __init__(self, stream=None):
        self.lines = []
        self.stream = stream

    def __call__(self, message):
        self.lines.append(message)
        if self.stream is not None:
            print(message, file=self.stream)


def file_sha256(path, chunk=1 << 20):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        while True:
            block = handle.read(chunk)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def _referenced_materials(data, material_ids):
    """Material pathIDs the component payload points at (PPtr = file id + int64 path id)."""
    found = []
    for path_id in material_ids:
        if struct.pack("<q", path_id) in data and path_id not in found:
            found.append(path_id)
    return found


def segment_textures(graph, catalog, log):
    """Addressable texture GUIDs of the hull segment materials.

    Multi-material segments swap their maps at runtime with
    textures that only exist in the addressables bundles, and stores them as
    (material property, asset GUID) string pairs.  Those pairs are the only
    link between a material and its 4096 px source, so the component payload is
    scanned for length prefixed "_Name" + 32 character GUID strings.

    Returns (material pathID -> {property: guid}, material name prefix ->
    addressable ship folder).
    """
    if catalog is None:
        return {}, {}
    serialized_file = graph.sf
    scripts = serialized_file.mono_scripts()
    material_ids = {info.path_id for info in serialized_file.objects_of(CLASS_MATERIAL)}
    tables = {}
    for path_id, script_id in serialized_file.component_script_path_ids().items():
        if scripts.get(script_id) != SEGMENT_COMPONENT:
            continue
        info = serialized_file.by_path.get(path_id)
        if info is None:
            continue
        data = serialized_file.object_data(info)
        pairs = _guid_pairs(data)
        if not pairs:
            continue
        # One segment component describes the maps of every material under it. Binding the table
        # only to the first one left most hull materials without a mask GUID, so they fell back to
        # the filename convention and picked up a neighbouring variant's paint mask.
        for target in _referenced_materials(data, material_ids):
            tables.setdefault(target, {}).update(pairs)

    roots = {}
    for path_id, table in tables.items():
        name = graph.value(path_id).get("m_Name") or ""
        prefix = name.split("_")[0].lower()
        if not prefix or prefix in roots:
            continue
        for slot in ("albedo", "mask", "normal"):
            guid = table.get(AA_PROPERTY[slot])
            found = catalog.resolve(guid) if guid else None
            if not found:
                continue
            roots[prefix] = os.path.dirname(os.path.dirname(found[0]))
            log("  addressable textures: %-22s -> %s" % (name, roots[prefix]))
            break
    log("  hull segment guid tables: %d materials, %d ship folders"
        % (len(tables), len(roots)))
    return tables, roots


def _referenced_material(data, material_ids):
    """pathID of the first Material a MonoBehaviour payload references."""
    found = None
    for offset in range(0, max(0, len(data) - 11), 4):
        path_id = struct.unpack_from("<q", data, offset + 4)[0]
        if path_id in material_ids:
            if found is not None and found != path_id:
                return found
            found = path_id
    return found


GUID_PAIR_NAME = re.compile(rb"_[A-Za-z0-9_]{2,40}")


def _guid_pairs(data):
    """{property: guid} from the length prefixed string pairs in a payload."""
    pairs = {}
    for offset in range(0, max(0, len(data) - 40)):
        length = struct.unpack_from("<I", data, offset)[0]
        if not 2 <= length <= 48 or offset + 4 + length > len(data):
            continue
        name = data[offset + 4:offset + 4 + length]
        if not GUID_PAIR_NAME.fullmatch(name):
            continue
        position = (offset + 4 + length + 3) & ~3
        if position + 36 > len(data):
            continue
        if struct.unpack_from("<I", data, position)[0] != 32:
            continue
        guid = data[position + 4:position + 36]
        if not all(char in b"0123456789abcdef" for char in guid):
            continue
        pairs[name.decode("ascii")] = guid.decode("ascii")
    return pairs


def _interleave(red, green, blue):
    """Pack three channels into one RGB payload."""
    values = bytearray(len(red) * 3)
    for i in range(len(red)):
        values[i * 3] = red[i]
        values[i * 3 + 1] = green[i]
        values[i * 3 + 2] = blue[i]
    return values


def _interleave_rgba(red, green, blue, alpha):
    """Pack four channels into one RGBA payload."""
    values = bytearray(len(red) * 4)
    for i in range(len(red)):
        values[i * 4] = red[i]
        values[i * 4 + 1] = green[i]
        values[i * 4 + 2] = blue[i]
        values[i * 4 + 3] = alpha[i]
    return values


class MaterialBuilder(object):
    """Decodes the textures a material needs and writes them as PNG.

    materialRole is "livery" for the materials a segment hands to the tint
    pass (_BasePaintTint / _StripePaintTint), even when no albedo texture
    resolves, and "fixed" for everything else: the game leaves those at their
    authored colour, so a copper doodad stays copper.  paintable mirrors the
    livery role; it only falls back to the name rule (FUNCTIONAL_MATERIALS) when
    the segment components could not be parsed at all.

    A texture slot resolves as: the addressable GUID the hull segment component
    stores -> the addressable "<Ship>_<Variant>_<Segment>" folder convention ->
    the texture the material references inside its own bundle -> a 4x4
    placeholder (masks only).
    """

    def __init__(self, graph, out_dir, sizes, resource_stream, log,
                 catalog=None, pool=None, guid_tables=None, ship_roots=None,
                 normals=True, livery=None, livery_fallback=False):
        self.graph = graph
        self.out_dir = out_dir
        self.sizes = sizes
        self.resource_stream = resource_stream
        self.log = log
        self.catalog = catalog
        self.pool = pool
        self.guid_tables = guid_tables or {}
        self.ship_roots = ship_roots or {}
        self.normals = normals
        self.livery = livery or {}
        self.livery_fallback = livery_fallback
        self.fallback_count = 0
        self.cache = {}
        self.png_cache = {}
        self.definitions = []
        self.by_path = {}
        self.failures = []
        self._by_name = None
        self.mask_sources = {}
        self.decal_declared = {}
        self.texture_sources = {slot[0]: {} for slot in AA_SLOTS}
        self.shared_pngs = 0
        self.names = {}
        self.fixed_names = []

    def material_role(self, path_id, name):
        """livery when a hull segment recolours the material, else fixed."""
        if path_id in self.livery:
            return "livery"
        if self.livery_fallback:
            self.fallback_count += 1
            return "fixed" if is_functional_material(name) else "livery"
        return "fixed"

    def texture_by_name(self):
        """name -> Texture2D pathID, for the addressable paint masks."""
        if self._by_name is None:
            names = {}
            for info in self.graph.sf.objects_of(28):
                name = self.graph.value(info.path_id).get("m_Name") or ""
                if name and name not in names:
                    names[name] = info.path_id
            self._by_name = names
        return self._by_name

    def _sibling_mask(self, environments):
        """Mask stored next to the albedo under the bundle naming convention.

        The authoring tool names the maps of one hull segment
        <segment>_base_color / <segment>_paint_mask / <segment>_normal, so a
        material without a _PaintMask slot still has a mask in its own bundle.
        """
        base = environments.get("_BaseColorMap") or environments.get("_MainTex")
        if not base:
            return None
        base_name = self.graph.value(base).get("m_Name") or ""
        for suffix in ("_base_color", "_basecolor", "_albedo", "_basecolour"):
            if base_name.endswith(suffix):
                candidate = base_name[:-len(suffix)] + "_paint_mask"
                return self.texture_by_name().get(candidate)
        return None

    def material_id(self, path_id):
        material = self.by_path.get(path_id)
        if material is not None:
            return material["id"]
        value = self.graph.value(path_id)
        properties = value.get("m_SavedProperties") or {}
        environments = {}
        for entry in properties.get("m_TexEnvs") or ():
            if not isinstance(entry, dict):
                continue
            name = entry.get("first")
            texture = entry.get("second") or {}
            path = hierarchy.pptr_path_id(texture.get("m_Texture"))
            if name and path:
                environments[name] = path
        colours = {}
        for entry in properties.get("m_Colors") or ():
            if not isinstance(entry, dict):
                continue
            name = entry.get("first")
            rgba = entry.get("second") or {}
            if name:
                colours[name] = (rgba.get("r", 1.0), rgba.get("g", 1.0),
                                 rgba.get("b", 1.0), rgba.get("a", 1.0))

        base = None
        for candidate in ("_BaseColor", "_Color", "_TintColor", "_BaseTint"):
            if candidate in colours:
                base = colours[candidate]
                break
        if base is None:
            base = (1.0, 1.0, 1.0, 1.0)

        name = value.get("m_Name") or ""
        sources = self._resolve_slots(path_id, name, environments)
        has_albedo_map = sources["albedo"][0] is not None
        role = self.material_role(path_id, name)
        paintable = role == "livery"
        if not paintable:
            self.fixed_names.append(name)
        index = len(self.definitions) + 1
        if paintable and not has_albedo_map:
            # No albedo to modulate: paint the livery straight onto the surface.
            # A grey authored tint here would darken the livery a second time.
            base_color = [1.0, 1.0, 1.0]
        else:
            base_color = [round(min(1.0, max(0.0, base[0])), 4),
                          round(min(1.0, max(0.0, base[1])), 4),
                          round(min(1.0, max(0.0, base[2])), 4)]
        definition = {
            "id": "t:%04d" % index,
            # livery = the game's tint pass really affects this
            # material (see livery_materials); fixed = authored colour kept.
            "paintable": paintable,
            "materialRole": role,
            "baseColor": base_color,
            "albedo": "",
            "albedoMode": "multiply",
            "albedoMix": 0.0,
            "mask": "",
            "maskChannel": "r",
            "maskInvert": False,
            "trimBaked": False,
            # HDRP _MaskMap means: the brass doodads are metallic=1 surfaces, and without a
            # specular lobe a fixed prop renders like flat plastic.
            "metallic": 0.0,
            "smoothness": 0.5,
            # trim: mask=0 takes the Trim colour (hull stripes). keep: mask=0 keeps the authored
            # albedo (brass props, grey deck plates) and only mask=1 is painted.
            "maskMode": "trim",
        }
        self.by_path[path_id] = definition
        self.names[definition["id"]] = name
        self.definitions.append(definition)

        decal_property = next((property_name for property_name
                               in slot_properties("decal")
                               if property_name in environments), "")
        self.decal_declared[definition["id"]] = decal_property

        albedo, albedo_origin = sources["albedo"]
        # A nameplate intentionally reuses the segment albedo: the plaque is part of the LOD and
        # its UVs live in the hull atlas, with the _nameplateDecal layered on top.
        if albedo:
            self._build_albedo(definition, albedo, albedo_origin)

        mask_map = environments.get("_MaskMap")
        if mask_map:
            self._build_mask_map(definition, ("local", mask_map))

        mask, mask_origin = sources["mask"]
        if mask:
            self._build_mask(definition, mask, mask_origin)
        elif definition["paintable"]:
            # No authored stripe mask: an all black mask keeps the renderer on
            # the base paint everywhere instead of falling back to the trim tint.
            relative = "mask/%s.png" % definition["id"].split(":")[1]
            textures.write_png(os.path.join(self.out_dir, relative), 4, 4, 1,
                               bytearray(16))
            definition["mask"] = relative
            self.mask_sources[definition["id"]] = "placeholder"
            self.log("    mask %s <- 4x4 placeholder (%r has no mask)"
                     % (definition["id"], name))

        if albedo and mask:
            definition["maskMode"] = self._mask_mode(albedo, sources["albedo"][0], mask)
            definition["maskInvert"] = definition["maskMode"] != "keep"
            # The Marauder (CivBulk) masks are authored the other way round: white marks the
            # contrasting paint band, so the trim weight is the raw mask value. Verified against
            # the in-game bow, where the hull is dominant orange with the hull symbols inside it.
            if (definition["maskMode"] == "trim"
                    and ("civbulk" in name.lower() or "marauder" in name.lower())):
                definition["maskInvert"] = False

        normal, normal_origin = sources["normal"]
        if normal:
            self._build_normal(definition, normal, normal_origin)

        decal, decal_origin = sources["decal"]
        if decal:
            self._build_decal(definition, decal, decal_origin)
        elif decal_property:
            self.log("    decal %s undecodable: %s is declared but no texture "
                     "resolved" % (definition["id"], decal_property))
        return definition["id"]

    # -- texture plumbing --------------------------------------------------
    def _resolve_slots(self, path_id, name, environments):
        """{slot: (source, origin)} for the hull maps of a material."""
        table = self.guid_tables.get(path_id) or {}
        slots = {}
        for slot in SLOT_NAMES:
            if slot == "normal" and not self.normals:
                slots[slot] = (None, "disabled")
                continue
            source, origin = self._resolve(slot, name, environments, table)
            counts = self.texture_sources[slot]
            counts[origin] = counts.get(origin, 0) + 1
            slots[slot] = (source, origin)
        return slots

    def _resolve(self, kind, name, environments, table):
        """(source, origin) of one texture slot.

        A source is ("guid", guid) for an addressable texture or
        ("local", pathID) for a texture that lives in the hull bundle itself.
        """
        for property_name in slot_properties(kind):
            guid = table.get(property_name)
            if guid and self.catalog is not None and self.catalog.resolve(guid):
                return ("guid", guid), "guid"
        guid = self._convention(name, kind, environments)
        if guid:
            return ("guid", guid), "convention"
        for property_name in slot_properties(kind):
            path_id = environments.get(property_name)
            if path_id:
                return ("local", path_id), "property"
        if kind == "albedo" and environments.get("_MainTex"):
            return ("local", environments["_MainTex"]), "property"
        if kind == "mask":
            sibling = self._sibling_mask(environments)
            if sibling:
                return ("local", sibling), "name"
        return None, "missing"

    def _convention(self, name, kind, environments):
        """Addressable texture of <Ship>_<A|B|C>_<Bow|Mid|Core|Stern>[_Tail]."""
        if self.catalog is None:
            return None
        match = None
        for pattern in SEGMENT_MATERIALS:
            match = pattern.match(name or "")
            if match:
                break
        if not match:
            return None
        root = self.ship_roots.get(match.group("ship").lower())
        if not root:
            root = self._local_root(environments)
        if not root:
            return None
        folder = "%s/%s Textures" % (root, match.group("variant").upper())
        segment = match.group("segment").lower()
        prefix = SEGMENT_ALIAS.get(segment, segment) + "_"
        return self.catalog.find_in_folder(folder, prefix, AA_SUFFIX[kind])

    def _local_root(self, environments):
        """Addressable ship folder deduced from the local albedo texture name.

        Materials the segment component does not describe ("CivBulk - A - Bow")
        still reference the lower resolution maps in their own bundle, and the
        texture name carries the ship code ("bulker_1_bow_base_color").
        """
        if self.catalog is None:
            return None
        for prop in ("_BaseColorMap", "_MainTex"):
            path_id = environments.get(prop)
            if not path_id:
                continue
            base = self.graph.value(path_id).get("m_Name") or ""
            for suffix in ("_base_color", "_basecolor", "_albedo", "_basecolour"):
                if base.lower().endswith(suffix):
                    code = base[:-len(suffix)].split("_")[0].lower()
                    return self.catalog.folder_roots.get(code)
        return None

    def _sample(self, slot, source):
        """(name, width, height, (r, g, b, a)) decoded at the slot's size."""
        size = self.sizes[slot]
        key = (source, size)
        cached = self.cache.get(key)
        if cached is not None:
            if isinstance(cached, Exception):
                raise cached
            return cached
        try:
            entry = self._sample_uncached(source, size)
        except Exception as exc:
            self.cache[key] = exc
            self.failures.append(("%s:%s" % (source[0], source[1]), str(exc)))
            raise
        self.cache[key] = entry
        return entry

    def _sample_uncached(self, source, size):
        if source[0] == "guid":
            found = self.pool.texture(source[1]) if self.pool is not None else None
            if found is None:
                raise textures.TextureError(
                    "addressable texture %s is not in its bundle" % (source[1],))
            serialized_file, path_id, stream = found
        else:
            serialized_file, path_id, stream = (self.graph.sf, source[1],
                                                self.resource_stream)
        info = textures.read_texture(serialized_file, path_id)
        level, _, _, _ = textures.pick_mip(info, size)
        width, height, red, green, blue, alpha = textures.decode_mip(
            info, level, stream)
        if width > size or height > size:
            red, width, height = textures.downsample(red, width, height, size)
            green, _, _ = textures.downsample(green, width, height, size)
            blue, _, _ = textures.downsample(blue, width, height, size)
            alpha, _, _ = textures.downsample(alpha, width, height, size)
        return info.name, width, height, (red, green, blue, alpha), info.format

    def _write(self, slot, definition, source, width, height, channels, values):
        """PNG path for a slot; identical sources share one file."""
        key = (slot, source)
        existing = self.png_cache.get(key)
        if existing is not None:
            self.shared_pngs += 1
            return existing
        relative = "%s/%s.png" % (slot, definition["id"].split(":")[1])
        textures.write_png(os.path.join(self.out_dir, relative), width, height,
                           channels, values)
        self.png_cache[key] = relative
        return relative

    def _build_mask(self, definition, source, origin):
        try:
            name, width, height, channels, _fmt = self._sample("mask", source)
        except Exception as exc:
            self.log("    mask %s undecodable (%s)" % (definition["id"], exc))
            definition["trimBaked"] = True
            self.mask_sources[definition["id"]] = "failed"
            return
        red, green, blue, alpha = channels
        stats = textures.channel_stats(red, green, blue, alpha)
        # Luminance masks (the authored _PaintMask is one) carry the stripe
        # pattern in every colour channel; averaging noise in the others would
        # only blur the polarity.
        channel = ("r" if textures.is_grayscale(red, green, blue)
                   else textures.choose_mask_channel(stats))
        values = {"r": red, "g": green, "b": blue, "a": alpha}[channel]
        definition["maskChannel"] = channel
        # The authored _PaintMask is WHITE where the base paint applies and BLACK
        # on the stripe/trim regions (flat plating versus seams, hatch rims,
        # grilles and vents). The in-game nameplate baker renders plates black
        # into this texture, i.e. it gives the plate the contrasting stripe tint
        # so the albedo lettering stays readable.  Renderers must therefore use
        # (1 - mask) as the trim weight; maskInvert says exactly that.
        # keep-mode materials paint only where the mask is white, so the raw value is wanted.
        definition["maskInvert"] = definition["maskMode"] != "keep"
        self.mask_sources[definition["id"]] = "%s:%s:%s" % (origin, channel, name)
        dark = sum(1 for value in values if value < 128) / float(len(values))
        self.log("    mask %s <- %-34s channel %s, dark %.0f%% (%s)"
                 % (definition["id"], name, channel, dark * 100.0, origin))
        values, width, height = textures.downsample(values, width, height,
                                                   self.sizes["mask"])
        definition["mask"] = self._write("mask", definition, source, width, height,
                                         1, values)

    def _mask_mode(self, albedo_source, mask_source_key, mask_source):
        """"keep" when the mask's zero side keeps a distinctly coloured authored albedo."""
        try:
            _n, _w, _h, channels, _f = self._sample("albedo", albedo_source)
            _m, _mw, _mh, mask_channels, _mf = self._sample("mask", mask_source)
        except Exception:
            return "trim"
        red, green, blue, _alpha = channels
        values = mask_channels[0]
        low = high = 0
        low_sat = high_sat = 0.0
        for index in range(0, len(values), 7):
            mask_index = index if index < len(values) else None
            if mask_index is None:
                continue
            pixel = index if index < len(red) else None
            if pixel is None:
                continue
            r, g, b = red[pixel], green[pixel], blue[pixel]
            top = max(r, g, b)
            sat = 0.0 if top == 0 else (top - min(r, g, b)) / float(top)
            if values[index] < 32:
                low += 1
                low_sat += sat
            elif values[index] > 223:
                high += 1
                high_sat += sat
        if not low or not high:
            return "trim"
        if (low_sat / low) > (high_sat / high) + 0.08:
            return "keep"
        # The doodads atlas carries painted-in metal on the unpainted side; the scatter test above
        # is noisy on it, so the family name decides.
        try:
            mask_name = self._sample("mask", mask_source)[0] or ""
        except Exception:
            mask_name = ""
        return "keep" if "doodads" in mask_name.lower() else "trim"

    def _build_mask_map(self, definition, source):
        """HDRP _MaskMap: R = metallic, G = occlusion, A = smoothness.

        Only the channel means are kept as scalars: the game's brass props need a metallic
        response, and shipping another 2048 square texture set for three numbers would not
        fit the asset budget.
        """
        try:
            name, _width, _height, channels, _fmt = self._sample_uncached(source, 64)
        except Exception as exc:
            self.log("    maskmap %s undecodable (%s)" % (definition["id"], exc))
            return
        red, _green, _blue, alpha = channels

        def mean(values):
            return sum(values) / float(len(values) or 1) / 255.0

        definition["metallic"] = round(mean(red), 4)
        definition["smoothness"] = round(mean(alpha), 4)
        self.log("    maskmap %s <- %-34s metallic %.2f smoothness %.2f"
                 % (definition["id"], name, definition["metallic"], definition["smoothness"]))

    def _build_albedo(self, definition, source, origin):
        try:
            name, width, height, channels, _fmt = self._sample("albedo", source)
        except Exception as exc:
            self.log("    albedo %s undecodable (%s)" % (definition["id"], exc))
            return
        red, green, blue, alpha = channels
        grey = textures.is_grayscale(red, green, blue)
        # multiply: rgb = paint * lerp(1, albedo, mix) -- 1.0 keeps the full
        # albedo detail, 0.0 would drop it entirely.
        # overlay: the albedo is added over the paint with a lighter weight.
        # A fixed material is not painted at all: HDRP multiplies baseColor * albedo at full
        # strength, which is what turns the grey doodads atlas into brass.
        full_strength = grey or not definition["paintable"]
        definition["albedoMode"] = "multiply" if full_strength else "overlay"
        definition["albedoMix"] = 1.0 if full_strength else 0.35
        if grey:
            values, channel_count = red, 1
        else:
            values, channel_count = _interleave(red, green, blue), 3
        definition["albedo"] = self._write("albedo", definition, source, width,
                                           height, channel_count, values)
        self.log("    albedo %s <- %-32s %s (%s)"
                 % (definition["id"], name, "grey" if grey else "rgb", origin))

    def _build_normal(self, definition, source, origin):
        try:
            name, width, height, channels, fmt = self._sample("normal", source)
        except Exception as exc:
            self.log("    normal %s undecodable (%s)" % (definition["id"], exc))
            return
        red, green, blue, alpha = channels
        if fmt == textures.FORMAT_BC3:
            # Unity stores tangent space normals as DXT5nm: X lives in alpha,
            # Y in green, and the colour channels carry no usable data.
            x = [v / 127.5 - 1.0 for v in alpha]
            y = [v / 127.5 - 1.0 for v in green]
            z = [max(0.0, 1.0 - a * a - b * b) ** 0.5 for a, b in zip(x, y)]
            red = [int((v * 0.5 + 0.5) * 255.0 + 0.5) for v in x]
            green = [int((v * 0.5 + 0.5) * 255.0 + 0.5) for v in y]
            blue = [int((v * 0.5 + 0.5) * 255.0 + 0.5) for v in z]
        values = _interleave(red, green, blue)
        definition["normal"] = self._write("normal", definition, source, width,
                                           height, 3, values)
        definition["normalStrength"] = 1.0
        self.log("    normal %s <- %-32s (%s)" % (definition["id"], name, origin))

    def _build_decal(self, definition, source, origin):
        """Nameplate decal; kept as RGBA when the source has an alpha mask."""
        try:
            name, width, height, channels, _fmt = self._sample("decal", source)
        except Exception as exc:
            self.log("    decal %s undecodable (%s)" % (definition["id"], exc))
            return
        red, green, blue, alpha = channels
        if any(value != 255 for value in alpha):
            # The authored decal is a glyph in RGB cut out by alpha (BC3), so
            # alpha is the shape and has to survive the export.
            values = _interleave_rgba(red, green, blue, alpha)
            channel_count = 4
            definition["decalMode"] = "alpha"
        else:
            values, channel_count = textures.luminance(red, green, blue), 1
            definition["decalMode"] = "luminance"
        definition["decal"] = self._write("decal", definition, source, width,
                                          height, channel_count, values)
        self.log("    decal %s <- %-30s %dx%d %s (%s)"
                 % (definition["id"], name, width, height,
                    "rgba" if channel_count == 4 else "grey", origin))


def _vec(values, digits=6):
    return [round(float(v), digits) + 0.0 for v in values]


class PieceBuilder(object):
    """Turns one Section into a catalog piece with baked .nlmesh files."""

    def __init__(self, graph, out_dir, materials, log):
        self.graph = graph
        self.out_dir = out_dir
        self.materials = materials
        self.log = log
        self.mesh_cache = {}
        self.written = {}
        self.mesh_count = 0
        self.vertex_total = 0
        self.index_total = 0

    def build(self, section, bake=True):
        """The catalog piece of one section.

        bake=False enumerates the same nodes (same order, same material slots)
        but writes no .nlmesh file, so a hull prefab that binds other materials
        to a shared piece can be read without duplicating geometry.
        """
        graph = self.graph
        nodes = []
        node_of_go = {}
        transforms = {}
        space = hierarchy.SectionSpace(graph, section.root_go)
        for depth, go in graph.walk(section.root_go):
            world = (space.transform_of_game_object(graph, go, transforms)
                     if bake else None)
            parent = graph.parent_of(go)
            entry = {
                "parent": node_of_go.get(parent, -1) if depth else -1,
                "p": _vec(IDENTITY_NODE[0]),
                "q": _vec(IDENTITY_NODE[1]),
                "s": _vec(IDENTITY_NODE[2]),
                "mesh": "",
                "material": "",
                "name": graph.name_of(go),
            }
            node_of_go[go] = len(nodes)
            nodes.append(entry)
            self._attach_geometry(go, world, entry, nodes, node_of_go, bake)

        piece = {
            "id": section.id,
            "key": section.key,
            "debugName": section.debug_name,
            "kind": "secondary" if section.kind == "superstructure" else "primary",
            "foreSnap": _snap(section.fore_snap),
            "aftSnap": _snap(section.aft_snap),
            "superstructureSnaps": [_snap(snap) for snap in section.superstructure_snaps],
            "dressingPoints": [],
            "nodes": nodes,
            "meshIds": [],
        }
        for point in section.dressing_points:
            variants = [node_of_go[child] for child in point.variant_roots
                        if child in node_of_go]
            piece["dressingPoints"].append({
                "index": point.index,
                "allowEmpty": point.allow_empty,
                "variants": variants,
                "variantNames": [graph.name_of(child) for child in point.variant_roots
                                 if child in node_of_go],
            })
        used = []
        for node in nodes:
            if node["mesh"] and node["mesh"] not in used:
                used.append(node["mesh"])
        piece["meshIds"] = used
        return piece

    def _attach_geometry(self, go, world, entry, nodes, node_of_go, bake=True):
        graph = self.graph
        filters = graph.components_by_class_id(go, hierarchy.CLASS_MESH_FILTER)
        if not filters:
            return
        mesh_id = hierarchy.pptr_path_id(graph.value(filters[0]).get("m_Mesh"))
        if not mesh_id:
            return
        renderers = graph.components_by_class_id(go, hierarchy.CLASS_MESH_RENDERER)
        material_paths = []
        if renderers:
            for material in graph.value(renderers[0]).get("m_Materials") or ():
                path_id = hierarchy.pptr_path_id(material)
                if path_id:
                    material_paths.append(path_id)
        try:
            mesh = meshes.read_mesh(self.graph.sf, mesh_id, self.mesh_cache)
        except Exception as exc:
            self.log("    mesh %d on %r skipped: %s" % (mesh_id,
                     graph.name_of(go), exc))
            return
        submeshes = mesh.submeshes or [(0, len(mesh.indices), 0, 0, 0,
                                        mesh.vertex_count)]
        targets = []
        for i, submesh in enumerate(submeshes):
            material_id = ""
            if material_paths:
                material_id = self.materials.material_id(
                    material_paths[min(i, len(material_paths) - 1)])
            targets.append((submesh, material_id))
        for i, (submesh, material_id) in enumerate(targets):
            if i == 0:
                node = entry
            else:
                node = {
                    "parent": node_of_go[go],
                    "p": _vec(IDENTITY_NODE[0]),
                    "q": _vec(IDENTITY_NODE[1]),
                    "s": _vec(IDENTITY_NODE[2]),
                    "mesh": "",
                    "material": "",
                    "name": "%s#%d" % (entry["name"], i),
                }
                nodes.append(node)
            if not submesh[1]:
                continue
            if bake:
                node["mesh"] = self._write_mesh(
                    mesh, mesh.submesh_indices(submesh), world)
            node["material"] = material_id

    def _write_mesh(self, mesh, indices, world):
        position, rotation, scale = world
        key = (mesh.name, len(indices), round(position[0], 4),
               round(position[1], 4), round(position[2], 4),
               round(rotation[0], 5), round(rotation[1], 5),
               round(rotation[2], 5), round(rotation[3], 5),
               round(scale[0], 5), round(scale[1], 5), round(scale[2], 5),
               round(indices[0], 0), round(indices[-1], 0))
        existing = self.written.get(key)
        if existing is not None:
            return existing
        identity = (position == (0.0, 0.0, 0.0)
                    and rotation == (0.0, 0.0, 0.0, 1.0)
                    and scale == (1.0, 1.0, 1.0))
        if identity:
            positions, normals = mesh.positions, mesh.normals
        else:
            positions = [mathutil.transform_point(position, rotation, scale, p)
                         for p in mesh.positions]
            normals = [mathutil.transform_normal(rotation, scale, n)
                       for n in mesh.normals]
        self.mesh_count += 1
        mesh_id = "m:%04d" % self.mesh_count
        path = os.path.join(self.out_dir, "mesh", mesh_id.split(":")[1] + ".nlmesh")
        size = meshes.write_nlmesh(path, positions, normals, mesh.uvs, indices)
        self.written[key] = mesh_id
        self.vertex_total += len(positions)
        self.index_total += len(indices)
        return mesh_id


def _snap(value):
    if value is None:
        return None
    position, rotation = value
    return {"p": _vec(position), "q": _vec(rotation)}


def paint_mask_score(graph, section, cache=None):
    """1 when any material under the section exposes a _PaintMask texture."""
    cache = cache if cache is not None else {}
    cached = cache.get(section.root_go)
    if cached is not None:
        return cached
    score = 0
    for _, go in graph.walk(section.root_go):
        for renderer in graph.components_by_class_id(go, hierarchy.CLASS_MESH_RENDERER):
            for material in graph.value(renderer).get("m_Materials") or ():
                path_id = hierarchy.pptr_path_id(material)
                if not path_id:
                    continue
                properties = graph.value(path_id).get("m_SavedProperties") or {}
                for entry in properties.get("m_TexEnvs") or ():
                    if entry.get("first") == "_PaintMask" and hierarchy.pptr_path_id(
                            (entry.get("second") or {}).get("m_Texture")):
                        score = 1
                        break
            if score:
                break
        if score:
            break
    cache[section.root_go] = score
    return score


def collect_pieces(graph, hulls):
    """Unique pieces keyed by GUID; shared keys prefer the instance that
    carries a _PaintMask (otherwise the stripe colour could never be shown)."""
    scores = {}
    candidates = {}
    order = []
    for save_key in HULLS:
        hull = hulls[save_key]
        for group in ("bows", "cores", "sterns", "superstructures"):
            for section in getattr(hull, group):
                if section.key not in candidates:
                    candidates[section.key] = []
                    order.append(section.key)
                candidates[section.key].append(section)
    chosen = []
    for key in order:
        best = None
        best_score = -1
        for section in candidates[key]:
            score = paint_mask_score(graph, section, scores)
            if score > best_score:
                best, best_score = section, score
        chosen.append(best)
    return chosen


def extract(bundle_path, out_dir, sizes=None, log=None, max_bytes=None,
            aa_dir=None, normals=True):
    log = log or Logger()
    sizes = dict(DEFAULT_SIZES, **(sizes or {}))
    started = time.time()
    folders = ("mesh", "mask", "albedo", "normal", "decal")
    for sub in folders:
        os.makedirs(os.path.join(out_dir, sub), exist_ok=True)
    for sub, suffix in (("mesh", ".nlmesh"), ("mask", ".png"),
                        ("albedo", ".png"), ("normal", ".png"),
                        ("decal", ".png")):
        folder = os.path.join(out_dir, sub)
        for name in os.listdir(folder):
            if name.endswith(suffix):
                os.remove(os.path.join(folder, name))

    bundle = unityfs.open_bundle(bundle_path)
    log("bundle: %s (%d blocks, %d nodes)"
        % (os.path.basename(bundle_path), bundle.block_count, len(bundle.nodes)))
    sf = serialized.open_from_bundle(bundle)
    log("serialized: %s, %d types, %d objects"
        % (sf.unity_version, len(sf.types), len(sf.objects)))
    graph = hierarchy.SceneGraph(sf)
    hulls = hierarchy.load_hull_types(graph)
    resource_stream = unityfs.resource_stream(bundle)
    aa_catalog = None
    pool = None
    if aa_dir:
        try:
            aa_catalog = addressables.load_catalog(aa_dir)
            pool = addressables.BundlePool(aa_catalog, log)
            log("addressables: %s, %d assets in %d bundles"
                % (aa_catalog.catalog_path, len(aa_catalog),
                   len(aa_catalog.bundle_names)))
        except Exception as exc:
            log("addressables: %s: %s (falling back to bundle textures)"
                % (type(exc).__name__, exc))
            aa_catalog = None
            pool = None
    (livery, livery_segments, livery_unresolved, livery_without_tint,
     livery_unmasked) = livery_materials(graph, hulls)
    livery_fallback = not livery
    log("livery: %d materials from %d hull segments%s"
        % (len(livery), livery_segments,
           "" if not livery_unresolved else
           ", %d segments unresolved %s" % (len(livery_unresolved),
                                             livery_unresolved[:4])))
    if livery_without_tint:
        log("  livery skipped, no %s property: %d %s"
            % (PAINT_TINT_PROPERTY, len(livery_without_tint),
               sorted(graph.value(path_id).get("m_Name") or ""
                      for path_id in livery_without_tint)[:6]))
    if livery_unmasked:
        log("  livery skipped, superstructure without %s: %d %s"
            % (PAINT_MASK_PROPERTY, len(livery_unmasked),
               sorted(graph.value(path_id).get("m_Name") or ""
                      for path_id in livery_unmasked)))
    if livery_fallback:
        log("  WARNING no hull segment resolved a livery material: falling back "
            "to the name based paintable rule")
    guid_tables, ship_roots = segment_textures(graph, aa_catalog, log)
    materials = MaterialBuilder(graph, out_dir, sizes, resource_stream, log,
                                catalog=aa_catalog, pool=pool,
                                guid_tables=guid_tables, ship_roots=ship_roots,
                                normals=normals, livery=livery,
                                livery_fallback=livery_fallback)
    pieces = collect_pieces(graph, hulls)
    log("hull types: %d, unique pieces: %d" % (len(hulls), len(pieces)))
    builder = PieceBuilder(graph, out_dir, materials, log)
    catalog_pieces = []
    for index, section in enumerate(pieces):
        hierarchy.resolve_snaps(graph, section)
        piece = builder.build(section)
        catalog_pieces.append(piece)
        log("  [%2d/%2d] %-10s %-28s nodes=%-3d dressing=%d meshes=%d"
            % (index + 1, len(pieces), piece["id"], piece["debugName"],
               len(piece["nodes"]), len(piece["dressingPoints"]),
               len(piece["meshIds"])))

    # One piece is one mesh (_key), but every hull prefab instance binds its own
    # materials to that mesh: the Bulk Hauler's bridge and the container liner's
    # are the same piece with a freighter and a lineship material.  collect_pieces
    # keeps the instance the exporter prefers, so each hull type records the node
    # materials its own instance carries.  Sparse: only nodes that differ.
    by_id = {piece["id"]: piece for piece in catalog_pieces}
    scan_started = time.time()
    scans = 0
    node_materials = {}
    instance_materials = {}
    node_conflicts = []
    node_shapes = []
    for save_key in HULLS:
        per_piece = {}
        per_piece_sets = {}
        signatures = {}
        for group in ("bows", "cores", "sterns", "superstructures"):
            for section in getattr(hulls[save_key], group):
                piece = by_id.get(section.id)
                if piece is None:
                    continue
                nodes = builder.build(section, bake=False)["nodes"]
                scans += 1
                signature = tuple(node["material"] for node in nodes)
                if section.id in signatures:
                    if signatures[section.id] != signature:
                        node_conflicts.append("%s %s: two instances disagree"
                                              % (save_key, section.id))
                    continue
                signatures[section.id] = signature
                if len(nodes) != len(piece["nodes"]):
                    # The hulls do not always bind the same node tree to a shared
                    # mesh (a refit toggles LOD children); the piece keeps the
                    # preferred instance, so only a differing material set is news.
                    shared = set(node["material"] for node in piece["nodes"]
                                 if node["material"])
                    if shared != set(m for m in signature if m):
                        node_conflicts.append(
                            "%s %s: instance binds other materials"
                            % (save_key, section.id))
                    else:
                        node_shapes.append("%s %s: %d nodes, piece has %d"
                                           % (save_key, section.id, len(nodes),
                                              len(piece["nodes"])))
                    continue
                overrides = {}
                for index, node in enumerate(nodes):
                    wanted = node["material"]
                    if wanted == piece["nodes"][index]["material"]:
                        continue
                    if not wanted:
                        node_conflicts.append("%s %s node %d: instance has no "
                                              "material" % (save_key, section.id,
                                                            index))
                        continue
                    overrides[str(index)] = wanted
                if overrides:
                    per_piece[section.id] = overrides
                per_piece_sets[section.id] = sorted(set(
                    material for material in signature if material))
        if per_piece:
            node_materials[save_key] = per_piece
        instance_materials[save_key] = per_piece_sets
    log("  instance scan: %d sections in %.1fs"
        % (scans, time.time() - scan_started))
    for label in node_shapes:
        log("  node tree differs from the shared piece, materials agree: %s"
            % label)
    for label in node_conflicts:
        log("  WARNING %s" % label)

    hull_entries = []
    for save_key, (role, container_path, display_name, prefab_path) in HULLS.items():
        hull = hulls[save_key]
        entry = {
            "id": save_key,
            "displayName": display_name,
            "role": role,
            "prefabPath": prefab_path,
            "editable": True,
            "bows": [section.id for section in hull.bows],
            "cores": [section.id for section in hull.cores],
            "sterns": [section.id for section in hull.sterns],
            "superstructures": [section.id for section in hull.superstructures],
        }
        if node_materials.get(save_key):
            # Shared superstructure meshes bind a different material per hull type: the freighter
            # bridge is Hull_Shader_NoPaint (authored brass), the lineship one is OSP_Hull_Proc
            # (tinted). The piece keeps the preferred instance, so each type records its own.
            entry["nodeMaterials"] = node_materials[save_key]
            log("  %s: %d pieces override node materials" % (
                save_key, len(node_materials[save_key])))
        hull_entries.append(entry)

    paintable_ids = {m["id"] for m in materials.definitions if m["paintable"]}
    fixed_names = sorted(set(materials.fixed_names))
    log("materials: %d livery, %d fixed-colour %s"
        % (len(paintable_ids), len(materials.definitions) - len(paintable_ids),
           fixed_names))
    if materials.fallback_count:
        log("  livery fallback: %d materials classified by the name rule"
            % materials.fallback_count)
    log("texture slots: %s" % ", ".join(
        "%s(%s)" % (slot, " ".join("%s=%d" % item for item in sorted(counts.items())))
        for slot, counts in materials.texture_sources.items()))
    log("shared texture files: %d" % materials.shared_pngs)

    # Every hull *body* material (the segment shells) must take the livery.
    # Node names alone are not enough: the LOD groups also exist for thrusters and
    # engine glows, so the hull body is identified by the segment naming
    # ("... Bow/Core/Stern/Mid ...").
    segment_words = ("bow", "core", "stern", "mid")
    hull_surface_issues = []
    container_surface = {}
    for piece in catalog_pieces:
        if piece["kind"] != "primary":
            continue
        for node in piece["nodes"]:
            if not node["mesh"] or not node["name"].upper().startswith("LOD"):
                continue
            material = node["material"]
            if not material:
                continue
            lowered = (materials.names.get(material) or "").lower()
            if not any(word in lowered for word in segment_words):
                continue
            if material not in paintable_ids:
                hull_surface_issues.append((piece["id"], node["name"],
                                            materials.names.get(material, material)))
    for save_key, (role, container_path, display_name, prefab_path) in HULLS.items():
        section_ids = [section.id for name in ("bows", "cores", "sterns")
                       for section in getattr(hulls[save_key], name)]
        involved = set()
        unpainted = []
        for section_id in section_ids:
            piece = by_id.get(section_id)
            if piece is None:
                continue
            for node in piece["nodes"]:
                if not node["mesh"] or not node["name"].upper().startswith("LOD"):
                    continue
                if not node["material"]:
                    continue
                name_lowered = (materials.names.get(node["material"]) or "").lower()
                if not any(word in name_lowered for word in segment_words):
                    continue
                involved.add(node["material"])
                if node["material"] not in paintable_ids:
                    unpainted.append(node["material"])
        container_surface[save_key] = {
            "materials": len(involved),
            "ids": sorted(involved),
            "unpainted": sorted(set(unpainted)),
            "names": sorted(materials.names.get(m, m) for m in involved),
        }
    log("hull surface check: %d unpainted %s"
        % (len(hull_surface_issues), hull_surface_issues[:4]))

    # Per hull type: every material its candidate pieces reference, split by the
    # role the game gives it.  The livery set has to stay a strict subset --
    # doodads, cranes and functional props keep their authored colour.
    by_material = {material["id"]: material for material in materials.definitions}

    def _role_ids(ids, role):
        return [material_id for material_id in ids
                if by_material.get(material_id, {}).get("materialRole") == role]

    hull_materials = {}
    for save_key in HULLS:
        referenced = []
        instances = instance_materials.get(save_key) or {}
        for group in ("bows", "cores", "sterns", "superstructures"):
            for section in getattr(hulls[save_key], group):
                materials_here = instances.get(section.id)
                if materials_here is None:
                    piece = by_id.get(section.id)
                    materials_here = [] if piece is None else [
                        node["material"] for node in piece["nodes"]]
                for material in materials_here:
                    if material and material not in referenced:
                        referenced.append(material)
        hull_materials[save_key] = {
            "referenced": referenced,
            "livery": _role_ids(referenced, "livery"),
            "fixed": _role_ids(referenced, "fixed"),
        }

    used_material_ids = {node["material"] for piece in catalog_pieces
                         for node in piece["nodes"] if node.get("material")}
    for overrides in node_materials.values():
        for table in overrides.values():
            used_material_ids.update(table.values())

    catalog = {
        "formatVersion": 1,
        "source": {
            "bundle": BUNDLE_NAME,
            "sha256": file_sha256(bundle_path),
            "unity": sf.unity_version,
            "extractor": EXTRACTOR_VERSION,
        },
        "hullTypes": hull_entries,
        "pieces": catalog_pieces,
        # Nothing may reference a material that no node binds: the instance scan can register a
        # material variant that the exported pieces do not use.
        "materials": [definition for definition in materials.definitions
                      if definition["id"] in used_material_ids],
    }
    path = os.path.join(out_dir, "catalog.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(catalog, handle, ensure_ascii=False, indent=1)
        handle.write("\n")
    elapsed = time.time() - started
    log("wrote %s (%.1f KiB) in %.1fs"
        % (path, os.path.getsize(path) / 1024.0, elapsed))
    log("meshes: %d files, %d vertices, %d indices"
        % (builder.mesh_count, builder.vertex_total, builder.index_total))
    stats = {
        "extractSeconds": round(elapsed, 2),
        "pieces": len(catalog_pieces),
        "meshes": builder.mesh_count,
        "vertices": builder.vertex_total,
        "indices": builder.index_total,
        "materials": len(catalog["materials"]),
        "paintable": len(paintable_ids),
        "materialNames": dict(materials.names),
        "materialPaths": {definition["id"]: path_id
                          for path_id, definition in materials.by_path.items()},
        "nodeMaterials": node_materials,
        "nodeMaterialConflicts": list(node_conflicts),
        "nodeShapeMismatches": list(node_shapes),
        "instanceMaterials": instance_materials,
        "hullMaterials": hull_materials,
        "livery": {
            "materials": len(livery),
            "segments": livery_segments,
            "unresolved": list(livery_unresolved),
            "withoutTint": sorted(graph.value(path_id).get("m_Name") or ""
                                  for path_id in livery_without_tint),
            "withoutPaintMask": sorted(graph.value(path_id).get("m_Name") or ""
                                       for path_id in livery_unmasked),
            "fallback": livery_fallback,
            "fallbackMaterials": materials.fallback_count,
        },
        "fixedMaterials": fixed_names,
        "hullSurfaceIssues": [list(item) for item in hull_surface_issues],
        "hullSurface": container_surface,
        "maskSources": dict(materials.mask_sources),
        "decalDeclared": dict(materials.decal_declared),
        "decals": {material["id"]: {"file": material["decal"],
                                    "mode": material["decalMode"]}
                   for material in materials.definitions
                   if material.get("decal")},
        "textureFailures": materials.failures,
        "textureSizes": dict(sizes),
        "textureSources": {slot: dict(counts)
                           for slot, counts in materials.texture_sources.items()},
        "sharedPngs": materials.shared_pngs,
        "addressables": {
            "catalog": aa_catalog.catalog_path if aa_catalog else "",
            "assets": len(aa_catalog) if aa_catalog else 0,
            "guidTables": len(guid_tables),
            "shipRoots": dict(ship_roots),
            "bundleFailures": [list(item) for item in
                               (pool.failures if pool is not None else [])],
        },
        "budgetBytes": max_bytes,
    }
    if pool is not None:
        pool.close()
    bundle.close()
    return catalog, stats
