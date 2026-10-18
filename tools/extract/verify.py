"""--verify: asserts the package invariants.

The documented dressing numbers for *core* segments are runtime aggregates: at
runtime the core becomes the parent of the assembled bow and stern, so a walk
over the children of the core component also counts the bow's and the stern's
dressing points.  This was reproduced exactly for every ship in
samples/*.fleet.
"""

from __future__ import annotations

import json
import os
import xml.etree.ElementTree as ElementTree

from . import export, hierarchy, meshes, serialized, textures, unityfs

#: material properties the decal slot may come from (export.SLOT_ALIASES)
DECAL_PROPERTIES = ("_nameplateDecal", "_DecalMap")

#: key prefix -> dressing count
DOC_BOWS = {
    "Stock/Bulk Hauler": [("c534a876", 2), ("38e7a28f", 3), ("29eb9c63", 3)],
    "Stock/Container Hauler": [("2d7c228c", 1), ("541cf476", 1)],
    "Stock/Container Hauler Refit": [("541cf476", 1), ("bb034299", 1)],
}
DOC_CORES = {
    "Stock/Bulk Hauler": [("e2c11e02", 8), ("d4c9a66d", 6), ("429f178e", 6)],
    "Stock/Container Hauler": [("18a6bc15", 4), ("09354e51", 3)],
    "Stock/Container Hauler Refit": [("2c68a462", 3), ("18a6bc15", 4)],
}
DOC_STERNS = {
    "Stock/Bulk Hauler": [("a8bf77b9", 0), ("78d72a9a", 0), ("2f2b451c", 0)],
    "Stock/Container Hauler": [("aff1eba2", 1), ("674e0528", 1), ("2dbd82fe", 1)],
    "Stock/Container Hauler Refit": [("aff1eba2", 1), ("674e0528", 1),
                                     ("2dbd82fe", 1)],
}
DOC_SUPERSTRUCTURES = {
    "Stock/Bulk Hauler": 4,
    "Stock/Container Hauler": 1,
    "Stock/Container Hauler Refit": 3,
}
HULL_IDS = ["Stock/Bulk Hauler", "Stock/Container Hauler",
            "Stock/Container Hauler Refit"]


class PathIdMaterials(object):
    """Material lookup that hands back the pathID instead of a catalog id.

    PieceBuilder maps renderer materials through a MaterialBuilder; the verifier
    only needs to know *which* material a node carries, so it re-reads the node
    list of a prefab instance without building (or exporting) any material.
    """

    def material_id(self, path_id):
        return path_id


class Report(object):
    def __init__(self):
        self.checks = []

    def check(self, name, ok, detail=""):
        self.checks.append((name, bool(ok), detail))
        return ok

    @property
    def failed(self):
        return [c for c in self.checks if not c[1]]

    def render(self, stream):
        for name, ok, detail in self.checks:
            print("%s %-46s %s" % ("PASS" if ok else "FAIL", name, detail),
                  file=stream)


def load_catalog(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def read_fleets(directory):
    """[(fleet file, hull type, [(key, dressing length) x3]), ...]"""
    ships = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".fleet"):
            continue
        tree = ElementTree.parse(os.path.join(directory, name))
        root = tree.getroot()
        for ship in root.findall(".//Ship"):
            hull = ship.findtext("HullType", "").strip()
            segments = []
            for segment in ship.findall(".//PrimaryStructure/SegmentConfiguration"):
                key = (segment.findtext("Key") or "").strip()
                dressing = segment.find("Dressing")
                length = 0 if dressing is None else len(dressing.findall("int"))
                segments.append((key, length))
            if hull and len(segments) == 3:
                ships.append((name, hull, segments))
    return ships


def verify(catalog, out_dir, stats, max_bytes=None, samples="samples",
           stream=None, bundle_path=None):
    import sys
    stream = stream or sys.stdout
    report = Report()
    pieces = {piece["id"]: piece for piece in catalog.get("pieces", [])}
    by_key = {}
    for piece in pieces.values():
        by_key[piece["key"].split("-")[0]] = piece
    hulls = {entry["id"]: entry for entry in catalog.get("hullTypes", [])}

    report.check("catalog formatVersion == 1", catalog.get("formatVersion") == 1,
                 str(catalog.get("formatVersion")))
    report.check("hullTypes ids are exactly the three stock liners",
                 sorted(hulls) == sorted(HULL_IDS), ", ".join(sorted(hulls)))

    # --- documented key / dressing table --------------------------------
    rows = 0
    found = set()
    missing = []
    wrong = []
    for hull_id in HULL_IDS:
        hull = hulls.get(hull_id, {})
        for group, table in (("bows", DOC_BOWS), ("cores", DOC_CORES),
                             ("sterns", DOC_STERNS)):
            candidate_keys = [pid.split(":")[1] for pid in hull.get(group, [])]
            for prefix, documented in table[hull_id]:
                rows += 1
                if prefix not in candidate_keys:
                    missing.append("%s %s %s" % (hull_id, group, prefix))
                    continue
                found.add(prefix)
                piece = by_key.get(prefix)
                own = len(piece["dressingPoints"]) if piece else -1
                if group in ("bows", "sterns"):
                    if own != documented:
                        wrong.append("%s %s own=%d doc=%d"
                                     % (prefix, group, own, documented))
                elif own > documented:
                    wrong.append("%s %s own=%d > doc=%d"
                                 % (prefix, group, own, documented))
    report.check("documented keys found (18 unique)", not missing
                 and len(found) == 18,
                 "%d/18 unique keys in %d table rows%s"
                 % (len(found), rows,
                    "" if not missing else "; missing " + ", ".join(missing[:3])))
    report.check("documented dressing counts reproduced", not wrong,
                 "; ".join(wrong[:4]) if wrong else "%d/%d rows" % (rows, rows))

    # --- sample fleets: exact dressing array lengths ---------------------
    ships = read_fleets(samples)
    segment_total = 0
    segment_bad = []
    used_supers = {hull_id: set() for hull_id in HULL_IDS}
    for fleet, hull_id, segments in ships:
        hull = hulls.get(hull_id)
        if not hull:
            segment_bad.append("%s: hull %r not in catalog" % (fleet, hull_id))
            continue
        classes = ["bows", "cores", "sterns"]
        own = []
        for index in range(3):
            key = segments[index][0].split("-")[0]
            piece = by_key.get(key)
            if piece is None:
                segment_bad.append("%s: key %s missing" % (fleet, key))
                own.append(-1)
                continue
            pool = [pid.split(":")[1] for pid in hull.get(classes[index], [])]
            if key not in pool:
                segment_bad.append("%s: %s not a %s candidate of %s"
                                   % (fleet, key, classes[index], hull_id))
            own.append(len(piece["dressingPoints"]))
        if len(own) != 3 or min(own) < 0:
            continue
        for index in range(3):
            segment_total += 1
            expected_length = own[index]
            if index == 1:
                expected_length += own[0] + own[2]
            if expected_length != segments[index][1]:
                segment_bad.append(
                    "%s seg%d %s: expected %d, fleet says %d"
                    % (fleet, index, segments[index][0].split("-")[0],
                       expected_length, segments[index][1]))
    report.check("sample fleet dressing arrays reproduced",
                 segment_total > 0 and not segment_bad,
                 "%d segments, %d mismatches" % (segment_total, len(segment_bad)))
    if segment_bad:
        for line in segment_bad[:5]:
            print("     " + line, file=stream)

    # superstructure counts (distinct keys used by the samples)
    for hull_id in HULL_IDS:
        documented = DOC_SUPERSTRUCTURES[hull_id]
        used = set()
        for fleet, fleet_hull, segments in ships:
            if fleet_hull != hull_id:
                continue
            tree = ElementTree.parse(os.path.join(samples, fleet))
            for ship in tree.getroot().findall(".//Ship"):
                if (ship.findtext("HullType") or "").strip() != hull_id:
                    continue
                for config in ship.findall(".//SecondaryStructureConfig"):
                    key = (config.findtext("Key") or "").split("-")[0]
                    if key:
                        used.add(key)
        report.check("superstructure count for %s" % hull_id,
                     len(used) == documented,
                     "%d distinct keys (doc %d)" % (len(used), documented))

    # --- structure integrity --------------------------------------------
    node_problems = []
    dressing_problems = []
    for piece in pieces.values():
        nodes = piece["nodes"]
        seen = 0
        for index, node in enumerate(nodes):
            parent = node["parent"]
            if parent >= index:
                node_problems.append("%s node %d parent %d" % (piece["id"], index, parent))
            if not node["name"]:
                node_problems.append("%s node %d unnamed" % (piece["id"], index))
            if node.get("mesh"):
                seen += 1
        for point in piece["dressingPoints"]:
            for variant in point["variants"]:
                if not 0 <= variant < len(nodes):
                    dressing_problems.append("%s dressing %d variant %d"
                                             % (piece["id"], point["index"], variant))
            if len(point["variants"]) != len(point["variantNames"]):
                dressing_problems.append("%s dressing %d names" % (piece["id"],
                                                                  point["index"]))
    report.check("node parents precede children", not node_problems,
                 "; ".join(node_problems[:3]) if node_problems else "all pieces")
    report.check("dressing variants index the node list", not dressing_problems,
                 "; ".join(dressing_problems[:3]) if dressing_problems else "all pieces")

    # --- meshes ----------------------------------------------------------
    mesh_files = 0
    mesh_bad = []
    no_uv = 0
    total_vertices = 0
    seen_meshes = set()
    for piece in pieces.values():
        for mesh_id in piece["meshIds"]:
            if mesh_id in seen_meshes:
                continue
            seen_meshes.add(mesh_id)
            path = os.path.join(out_dir, "mesh", mesh_id.split(":")[1] + ".nlmesh")
            if not os.path.exists(path):
                mesh_bad.append("%s missing" % mesh_id)
                continue
            data = meshes.read_nlmesh(path)
            mesh_files += 1
            total_vertices += data["vertexCount"]
            if data["vertexCount"] <= 0:
                mesh_bad.append("%s has no vertices" % mesh_id)
            if not data["uvs"]:
                no_uv += 1
                mesh_bad.append("%s has no uv0" % mesh_id)
            if data["indexCount"] <= 0:
                mesh_bad.append("%s has no indices" % mesh_id)
            if data["indices"] and max(data["indices"]) >= data["vertexCount"]:
                mesh_bad.append("%s index out of range" % mesh_id)
    report.check("every exported mesh has vertices, uv0 and indices",
                 not mesh_bad and mesh_files > 0,
                 "%d files, %d vertices, %d without uv0"
                 % (mesh_files, total_vertices, no_uv))

    # --- textures --------------------------------------------------------
    materials = catalog.get("materials", [])
    with_mask = [m for m in materials if m["paintable"]]
    masks_ok = [m for m in with_mask if m["mask"]]
    with_albedo = [m for m in materials if m["albedo"]]
    albedo_ok = [m for m in with_albedo if m["albedo"]]
    texture_total = len(with_mask) + len(with_albedo)
    texture_ok = len(masks_ok) + len(albedo_ok)
    rate = (texture_ok / texture_total) if texture_total else 1.0
    report.check("texture decode rate >= 95%", rate >= 0.95,
                 "%d/%d (%.1f%%)" % (texture_ok, texture_total, rate * 100.0))

    # --- paintable policy (ownership based, see export.MaterialBuilder) ---
    paintable = [m for m in materials if m["paintable"]]
    fixed = [m for m in materials if not m["paintable"]]
    issues = stats.get("hullSurfaceIssues") or []
    report.check("every hull body material is paintable", not issues,
                 "%d paintable / %d fixed%s" % (
                     len(paintable), len(fixed),
                     "" if not issues else "; unpainted " + str(issues[:3])))
    surfaces = stats.get("hullSurface") or {}
    container = surfaces.get("Stock/Container Hauler")
    empty_base = [m["id"] for m in paintable if not m["albedo"]
                  and m["baseColor"] != [1.0, 1.0, 1.0]]
    report.check("container hull surfaces take the livery",
                 bool(container) and not container.get("unpainted")
                 and not empty_base,
                 "%d materials (%s), neutral baseColor %s" % (
                     (container or {}).get("materials", 0),
                     ", ".join((container or {}).get("names", [])[:4]),
                     "ok" if not empty_base else str(empty_base[:3])))

    # --- livery policy (export.livery_materials) --------------------------
    # The game recolours exactly the materials reachable through a hull piece's
    # paintable meshes.  Being referenced by a hull piece is not enough: doodads,
    # cranes and side tanks keep their authored copper.
    names = stats.get("materialNames") or {}
    livery_materials = [material for material in materials
                        if material.get("materialRole") == "livery"]
    fixed_materials = [material for material in materials
                       if material.get("materialRole") == "fixed"]
    unroled = [material["id"] for material in materials
               if material.get("materialRole") not in ("livery", "fixed")]
    report.check("every material declares materialRole", not unroled,
                 "%d livery / %d fixed%s"
                 % (len(livery_materials), len(fixed_materials),
                    "" if not unroled else "; undeclared " + str(unroled[:3])))
    mismatched = [material["id"] for material in materials
                  if material["paintable"]
                  != (material.get("materialRole") == "livery")]
    report.check("paintable mirrors materialRole == livery", not mismatched,
                 "%d materials" % len(materials) if not mismatched
                 else "; ".join(mismatched[:4]))
    livery_stats = stats.get("livery") or {}
    report.check("livery resolved from the hull segment components",
                 bool(livery_stats.get("segments")) and not livery_stats.get("fallback"),
                 "%d segments -> %d materials, %d name-rule fallbacks%s"
                 % (livery_stats.get("segments", 0), len(livery_materials),
                    livery_stats.get("fallbackMaterials") or 0,
                    "" if not livery_stats.get("unresolved") else
                    "; unresolved " + ", ".join(livery_stats["unresolved"][:3])))

    # --- livery per hull type: non-empty strict subset --------------------
    hull_roles = stats.get("hullMaterials") or {}
    role_counts = []
    role_bad = []
    for hull_id in HULL_IDS:
        entry = hull_roles.get(hull_id) or {}
        referenced = entry.get("referenced") or []
        livery_ids = entry.get("livery") or []
        role_counts.append("%s %d/%d"
                           % (hull_id.split("/")[-1], len(livery_ids), len(referenced)))
        if not livery_ids or len(livery_ids) >= len(referenced):
            role_bad.append("%s livery=%d referenced=%d"
                            % (hull_id, len(livery_ids), len(referenced)))
    report.check("every hull type paints a strict subset of its materials",
                 bool(hull_roles) and not role_bad,
                 ", ".join(role_counts) if not role_bad else "; ".join(role_bad))
    for hull_id in HULL_IDS:
        entry = hull_roles.get(hull_id) or {}
        for role in ("livery", "fixed"):
            ids = entry.get(role) or []
            print("     %-15s %-6s %2d %s"
                  % (hull_id.split("/")[-1], role, len(ids),
                     ", ".join("%s %s" % (material_id, names.get(material_id, "?"))
                               for material_id in ids)), file=stream)

    # --- per hull type node materials -------------------------------------
    # One piece is one mesh; the hull prefabs bind their own materials to it.
    # hullTypes[i].nodeMaterials overrides the catalog piece's node material at a
    # sparse set of node indices, so these checks pin the shape, the id space and
    # the two properties the renderer relies on: the Bulk Hauler's bridge must
    # come out unpainted and the container liner's painted.
    piece_nodes = {piece["id"]: piece["nodes"] for piece in pieces.values()}
    material_ids = {material["id"] for material in materials}
    overrides = {hull_id: (hulls.get(hull_id) or {}).get("nodeMaterials") or {}
                 for hull_id in HULL_IDS}
    shape_issues = []
    id_issues = []
    for hull_id in HULL_IDS:
        for piece_id, node_table in overrides[hull_id].items():
            if not isinstance(node_table, dict):
                shape_issues.append("%s %s is not an object" % (hull_id, piece_id))
                continue
            if piece_id not in piece_nodes:
                shape_issues.append("%s %s: no such piece" % (hull_id, piece_id))
                continue
            if not node_table:
                shape_issues.append("%s %s is empty" % (hull_id, piece_id))
            for key, material_id in node_table.items():
                try:
                    index = int(key)
                except (TypeError, ValueError):
                    shape_issues.append("%s %s key %r is not a node index"
                                        % (hull_id, piece_id, key))
                    continue
                if not 0 <= index < len(piece_nodes[piece_id]):
                    shape_issues.append(
                        "%s %s node %s out of range (%d nodes)"
                        % (hull_id, piece_id, key, len(piece_nodes[piece_id])))
                if material_id not in material_ids:
                    id_issues.append("%s %s node %s: %s"
                                     % (hull_id, piece_id, key, material_id))
    report.check("nodeMaterials indexes existing pieces and nodes",
                 not shape_issues,
                 "%d hull types, %d pieces%s"
                 % (len(HULL_IDS),
                    sum(len(table) for table in overrides.values()),
                    "" if not shape_issues else "; " + "; ".join(shape_issues[:3])))
    report.check("nodeMaterials only names catalog materials", not id_issues,
                 "%d ids%s" % (len(material_ids),
                               "" if not id_issues else
                               "; " + "; ".join(id_issues[:3])))

    def applied(hull_id, piece_id):
        """Material ids of a piece once the hull type's overrides are applied."""
        piece = pieces.get(piece_id)
        if piece is None:
            return set()
        table = overrides.get(hull_id, {}).get(piece_id) or {}
        found = set()
        for index, node in enumerate(piece["nodes"]):
            material_id = table.get(str(index), node["material"])
            if material_id:
                found.add(material_id)
        return found

    # Every superstructure piece is tinted by the game in all hull types (the freighter and the
    # lineship bridge materials both declare _BasePaintTint), so the shipped catalog must keep
    # the shared pieces livery instead of overriding them per hull type.
    stale = [hull.get("id") for hull in hulls.values() if hull.get("nodeMaterials")]
    report.check("no per-hull-type material overrides", not stale,
                 "shared superstructure pieces are tinted in every hull type"
                 if not stale else "; ".join(stale[:3]))

    doodads = [material for material in materials
               if material["id"] == "t:0004"
               or "doodad" in (names.get(material["id"]) or "").lower()]
    doodads_painted = ["%s %s=%s" % (material["id"],
                                     names.get(material["id"], "?"),
                                     material.get("materialRole"))
                       for material in doodads
                       if material.get("materialRole") != "fixed"]
    report.check("doodads keep their authored colour (fixed)",
                 bool(doodads) and not doodads_painted,
                 "%d materials %s" % (len(doodads), ", ".join(
                     "%s %s=%s" % (material["id"], names.get(material["id"], "?"),
                                   material.get("materialRole"))
                     for material in doodads)) if not doodads_painted
                 else "; ".join(doodads_painted))

    # Every hull body material must carry authored maps: the addressable
    # texture bundles hold the 4096 px sources, so a 4x4 placeholder means the
    # GUID / folder resolution failed for that material.
    surfaces = stats.get("hullSurface") or {}
    by_id = {material["id"]: material for material in materials}
    texture_files = {}
    placeholders = set()
    unreadable = []
    dark_ratios = []
    decal_pixels = {}
    for material in materials:
        for kind in ("albedo", "mask", "normal", "decal"):
            relative = material.get(kind) or ""
            if not relative:
                continue
            path = os.path.join(out_dir, relative)
            if not os.path.exists(path):
                unreadable.append("%s is missing" % relative)
                continue
            try:
                width, height, channels, pixels = textures.read_png(path)
            except Exception as exc:
                unreadable.append("%s: %s" % (relative, exc))
                continue
            # only a decal keeps its alpha channel (RGBA, colour type 6)
            if channels not in ((1, 4) if kind == "decal" else (1, 3)):
                unreadable.append("%s has %d channels" % (relative, channels))
            texture_files.setdefault(kind, set()).add(relative)
            if kind == "decal":
                decal_pixels[material["id"]] = (channels, width, height)
            if width <= 4 and height <= 4:
                placeholders.add((material["id"], kind))
                continue
            if kind == "mask":
                luminance = pixels[::channels]
                dark = sum(1 for value in luminance if value < 128) / float(len(luminance))
                dark_ratios.append((material["id"], dark))

    hull_materials = []
    unresolved = []
    for hull_id in HULL_IDS:
        for material_id in (surfaces.get(hull_id) or {}).get("ids") or []:
            material = by_id.get(material_id)
            if material is None:
                unresolved.append("%s: %s is not in the catalog"
                                  % (hull_id, material_id))
            elif material not in hull_materials:
                hull_materials.append(material)
    hull_bad = [(material["id"], kind) for material in hull_materials
                for kind in ("albedo", "mask")
                if not material.get(kind)
                or (material["id"], kind) in placeholders]
    report.check("every hull body material has a real albedo and mask",
                 bool(hull_materials) and not hull_bad and not unresolved,
                 "%d hull materials, %d placeholder/missing %s%s"
                 % (len(hull_materials), len(hull_bad),
                    ", ".join("%s %s" % item for item in hull_bad[:4]),
                    "" if not unresolved else "; missing " + unresolved[0]))
    livery_no_mask = ["%s %s" % (material["id"], names.get(material["id"], "?"))
                      for material in livery_materials
                      if not material.get("mask")
                      or (material["id"], "mask") in placeholders]
    report.check("every livery material has a real (non placeholder) mask",
                 bool(livery_materials) and not livery_no_mask,
                 "%d livery materials%s"
                 % (len(livery_materials),
                    "" if not livery_no_mask
                    else "; placeholder/missing " + ", ".join(livery_no_mask[:4])))

    bad_dark = [item for item in dark_ratios if not 0.2 < item[1] < 0.8]
    ratio_span = "%.2f-%.2f" % (min(item[1] for item in dark_ratios),
                                max(item[1] for item in dark_ratios)) \
        if dark_ratios else "no masks"
    report.check("hull mask dark ratio within 0.2-0.8",
                 bool(dark_ratios) and not bad_dark,
                 "%d masks, %s%s" % (len(dark_ratios), ratio_span,
                                     "" if not bad_dark else
                                     "; out of range " + str(bad_dark[:3])))

    placeholder_masks = {item[0] for item in placeholders if item[1] == "mask"}
    polarity_bad = []
    for material in materials:
        if not material.get("mask"):
            continue
        if material["id"] in placeholder_masks:
            if material["maskInvert"]:
                polarity_bad.append(material["id"] + " placeholder inverted")
        elif material["maskChannel"] != "r" or not material["maskInvert"]:
            polarity_bad.append(material["id"])
    report.check("mask polarity is (1 - mask) on authored masks",
                 not polarity_bad,
                 "%d masks, channel/polarity %s"
                 % (len(materials), "ok" if not polarity_bad
                    else "; ".join(polarity_bad[:3])))

    hull_with_normal = [material for material in hull_materials
                        if material.get("normal")]
    coverage = (len(hull_with_normal) / float(len(hull_materials))
                if hull_materials else 0.0)
    report.check("hull normal map coverage >= 90%", coverage >= 0.9,
                 "%d/%d (%.0f%%)" % (len(hull_with_normal), len(hull_materials),
                                    coverage * 100.0))
    report.check("every referenced PNG decodes as 8 bit", not unreadable,
                 "; ".join(unreadable[:3]) if unreadable else
                 "%d files, %d materials"
                 % (sum(len(files) for files in texture_files.values()),
                    len(materials)))
    # --- nameplate decals ------------------------------------------------
    # The decal slot must appear exactly on the materials whose source declares
    # _nameplateDecal / _DecalMap: a catalog entry without the property is an
    # invented slot, a declared property without an entry a silent drop.
    declared = stats.get("decalDeclared") or {}
    with_decal = [material for material in materials if material.get("decal")]
    decal_slot_issues = []
    for material in materials:
        declared_property = declared.get(material["id"]) or ""
        exported = material.get("decal") or ""
        if bool(declared_property) != bool(exported):
            decal_slot_issues.append(
                "%s %s declared=%r exported=%r" % (material["id"], names.get(
                    material["id"], "?"), declared_property, exported or "-"))
        elif declared_property and declared_property not in DECAL_PROPERTIES:
            decal_slot_issues.append("%s property %r" % (material["id"],
                                                         declared_property))
    report.check("decal slot only on materials declaring _nameplateDecal",
                 bool(with_decal) and not decal_slot_issues,
                 "%d materials declare, %d export %s%s"
                 % (len([value for value in declared.values() if value]),
                    len(with_decal),
                    ", ".join(sorted({material["decal"]
                                     for material in with_decal})),
                    "" if not decal_slot_issues else
                    "; " + ", ".join(decal_slot_issues[:3])))
    decal_bad = []
    for material in with_decal:
        relative = material["decal"]
        mode = material.get("decalMode")
        if mode not in ("alpha", "luminance"):
            decal_bad.append("%s decalMode %r" % (material["id"], mode))
        record = decal_pixels.get(material["id"])
        if record is None:
            decal_bad.append("%s %s missing" % (material["id"], relative))
            continue
        channels, width, height = record
        if channels != (4 if mode == "alpha" else 1):
            decal_bad.append("%s %s has %d channels" % (material["id"], mode,
                                                        channels))
        if width <= 4 or height <= 4:
            decal_bad.append("%s is a placeholder" % material["id"])
        if not relative.startswith("decal/"):
            decal_bad.append("%s not under decal/" % relative)
    report.check("every decal PNG is real and matches its decalMode",
                 bool(with_decal) and not decal_bad,
                 "%d decals (%s)%s"
                 % (len(with_decal),
                    ", ".join("%s %s %s" % (material["id"], material["decal"],
                                            material.get("decalMode"))
                              for material in with_decal[:2]),
                    "" if not decal_bad else "; " + ", ".join(decal_bad[:3])))

    texture_counts = {}
    for kind in ("albedo", "mask", "normal", "decal"):
        files = texture_files.get(kind) or set()
        size = sum(os.path.getsize(os.path.join(out_dir, name)) for name in files)
        texture_counts[kind] = (len(files), size)
        print("     %-6s %3d files %8.2f KiB" % (kind, len(files), size / 1024.0),
              file=stream)

    # --- volume ----------------------------------------------------------
    total = 0
    category_bytes = {}
    for root, _, files in os.walk(out_dir):
        category = os.path.basename(root) or "."
        for name in files:
            size = os.path.getsize(os.path.join(root, name))
            total += size
            category_bytes[category] = category_bytes.get(category, 0) + size
    limit = max_bytes if max_bytes else 60 * 1024 * 1024
    breakdown = ", ".join("%s %.1f" % (name, category_bytes[name] / 1048576.0)
                          for name in sorted(category_bytes))
    report.check("assets/hulls within size budget", total <= limit,
                 "%.2f MiB / %.2f MiB (%s)"
                 % (total / 1048576.0, limit / 1048576.0, breakdown))
    return report, {"meshFiles": mesh_files, "vertices": total_vertices,
                    "textureRate": round(rate, 4), "totalBytes": total,
                    "categoryBytes": category_bytes,
                    "textureCounts": {kind: counts[0]
                                      for kind, counts in texture_counts.items()},
                    "maskDark": [(material_id, round(value, 3))
                                 for material_id, value in dark_ratios],
                    "normalCoverage": round(coverage, 3),
                    "hullMaterials": len(hull_materials)}
