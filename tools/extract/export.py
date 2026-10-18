"""Builds assets/hulls (catalog.json + .nlmesh + PNG) from a Unity bundle and
verifies the result against the acceptance thresholds.
"""

from __future__ import annotations

import hashlib
import json
import os
import time

from . import hierarchy, mathutil, meshes, serialized, textures, unityfs

EXTRACTOR_VERSION = "0.1.0"
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


class MaterialBuilder(object):
    """Decodes the textures a material needs and writes them as PNG."""

    def __init__(self, graph, out_dir, tex_size, resource_stream, log):
        self.graph = graph
        self.out_dir = out_dir
        self.tex_size = tex_size
        self.resource_stream = resource_stream
        self.log = log
        self.cache = {}
        self.definitions = []
        self.by_path = {}
        self.failures = []

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

        textured = bool(environments.get("_BaseColorMap")
                        or environments.get("_MainTex"))
        index = len(self.definitions) + 1
        definition = {
            "id": "t:%04d" % index,
            # A material is paintable when it carries hull surface information
            # (albedo and/or a _PaintMask); plain effect materials such as
            # Windows or Engine Glow keep their authored colour instead.
            "paintable": textured or "_PaintMask" in environments,
            "baseColor": [round(min(1.0, max(0.0, base[0])), 4),
                          round(min(1.0, max(0.0, base[1])), 4),
                          round(min(1.0, max(0.0, base[2])), 4)],
            "albedo": "",
            "albedoMode": "multiply",
            "albedoMix": 0.0,
            "mask": "",
            "maskChannel": "r",
            "maskInvert": False,
            "trimBaked": False,
        }
        self.by_path[path_id] = definition
        self.definitions.append(definition)

        mask_path = environments.get("_PaintMask")
        if mask_path:
            self._build_mask(definition, mask_path)
        elif definition["paintable"]:
            # No authored stripe mask: an all black mask keeps the renderer on
            # the base paint everywhere instead of falling back to the trim tint.
            relative = "mask/%s.png" % definition["id"].split(":")[1]
            textures.write_png(os.path.join(self.out_dir, relative), 4, 4, 1,
                               bytearray(16))
            definition["mask"] = relative

        albedo_path = environments.get("_BaseColorMap") or environments.get("_MainTex")
        if albedo_path:
            self._build_albedo(definition, albedo_path)
        return definition["id"]

    # -- texture plumbing --------------------------------------------------
    def _texture(self, path_id):
        cached = self.cache.get(path_id)
        if cached is not None:
            if isinstance(cached, Exception):
                raise cached
            return cached
        try:
            info = textures.read_texture(self.graph.sf, path_id)
            level, _, width, height = textures.pick_mip(info, self.tex_size)
            decoded = textures.decode_mip(info, level, self.resource_stream)
            entry = (info, level, decoded)
            self.cache[path_id] = entry
            return entry
        except Exception as exc:
            self.cache[path_id] = exc
            self.failures.append((path_id, str(exc)))
            raise

    def _build_mask(self, definition, path_id):
        try:
            info, level, (width, height, red, green, blue, alpha) = self._texture(path_id)
        except Exception as exc:
            self.log("    mask %s undecodable (%s) -> per vertex trim fallback"
                     % (definition["id"], exc))
            definition["trimBaked"] = True
            return
        stats = textures.channel_stats(red, green, blue, alpha)
        channel = textures.choose_mask_channel(stats)
        values = {"r": red, "g": green, "b": blue, "a": alpha}[channel]
        mean = stats[channel][0] if channel in stats else 0.0
        definition["maskChannel"] = channel
        definition["maskInvert"] = mean > 0.5
        if definition["maskInvert"]:
            values = bytearray(255 - value for value in values)
        values, out_width, out_height = textures.downsample(values, width, height,
                                                           self.tex_size)
        relative = "mask/%s.png" % definition["id"].split(":")[1]
        textures.write_png(os.path.join(self.out_dir, relative), out_width,
                           out_height, 1, values)
        definition["mask"] = relative

    def _build_albedo(self, definition, path_id):
        try:
            info, level, (width, height, red, green, blue, alpha) = self._texture(path_id)
        except Exception as exc:
            self.log("    albedo %s undecodable (%s)" % (definition["id"], exc))
            return
        grey = textures.is_grayscale(red, green, blue)
        definition["albedoMode"] = "multiply" if grey else "overlay"
        # multiply: albedo multiplies the paint (0.0, matches the doc example);
        # overlay: albedo adds detail over the paint, needs a blend weight.
        definition["albedoMix"] = 0.0 if grey else 0.35
        if grey:
            values, out_width, out_height = textures.downsample(red, width, height,
                                                               self.tex_size)
            channels = 1
        else:
            red, out_width, out_height = textures.downsample(red, width, height,
                                                            self.tex_size)
            green, _, _ = textures.downsample(green, width, height, self.tex_size)
            blue, _, _ = textures.downsample(blue, width, height, self.tex_size)
            values = bytearray(out_width * out_height * 3)
            for i in range(out_width * out_height):
                values[i * 3] = red[i]
                values[i * 3 + 1] = green[i]
                values[i * 3 + 2] = blue[i]
            channels = 3
        relative = "albedo/%s.png" % definition["id"].split(":")[1]
        textures.write_png(os.path.join(self.out_dir, relative), out_width,
                           out_height, channels, values)
        definition["albedo"] = relative


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

    def build(self, section):
        graph = self.graph
        nodes = []
        node_of_go = {}
        transforms = {}
        space = hierarchy.SectionSpace(graph, section.root_go)
        for depth, go in graph.walk(section.root_go):
            world = space.transform_of_game_object(graph, go, transforms)
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
            self._attach_geometry(go, world, entry, nodes, node_of_go)

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

    def _attach_geometry(self, go, world, entry, nodes, node_of_go):
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
            indices = mesh.submesh_indices(submesh)
            if not indices:
                continue
            node["mesh"] = self._write_mesh(mesh, indices, world)
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


def extract(bundle_path, out_dir, tex_size=512, log=None, max_bytes=None):
    log = log or Logger()
    started = time.time()
    for sub in ("mesh", "mask", "albedo"):
        os.makedirs(os.path.join(out_dir, sub), exist_ok=True)
    for sub, suffix in (("mesh", ".nlmesh"), ("mask", ".png"), ("albedo", ".png")):
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
    resource_stream = None
    for node in bundle.nodes:
        if node.path.lower().endswith(".ress"):
            resource_stream = unityfs.NodeStream(bundle, node)
            break
    materials = MaterialBuilder(graph, out_dir, tex_size, resource_stream, log)
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

    hull_entries = []
    for save_key, (role, container_path, display_name, prefab_path) in HULLS.items():
        hull = hulls[save_key]
        hull_entries.append({
            "id": save_key,
            "displayName": display_name,
            "role": role,
            "prefabPath": prefab_path,
            "editable": True,
            "bows": [section.id for section in hull.bows],
            "cores": [section.id for section in hull.cores],
            "sterns": [section.id for section in hull.sterns],
            "superstructures": [section.id for section in hull.superstructures],
        })

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
        "materials": materials.definitions,
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
        "materials": len(materials.definitions),
        "textureFailures": materials.failures,
        "budgetBytes": max_bytes,
    }
    bundle.close()
    return catalog, stats
