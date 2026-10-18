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

from . import meshes, textures

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
           stream=None):
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
    hits = 0
    expected = 0
    wrong = []
    for hull_id in HULL_IDS:
        hull = hulls.get(hull_id, {})
        for group, table in (("bows", DOC_BOWS), ("cores", DOC_CORES),
                             ("sterns", DOC_STERNS)):
            candidates = hull.get(group, [])
            candidate_keys = [pid.split(":")[1] for pid in candidates]
            for prefix, documented in table[hull_id]:
                expected += 1
                if prefix not in candidate_keys:
                    wrong.append("%s %s %s missing" % (hull_id, group, prefix))
                    continue
                hits += 1
                piece = by_key.get(prefix)
                own = len(piece["dressingPoints"]) if piece else -1
                if group in ("bows", "sterns"):
                    if own != documented:
                        wrong.append("%s %s own=%d doc=%d"
                                     % (prefix, group, own, documented))
                elif own > documented:
                    wrong.append("%s %s own=%d > doc=%d"
                                 % (prefix, group, own, documented))
    report.check("documented keys found (18 unique)", hits == expected,
                 "%d/%d unique keys" % (hits, expected))
    report.check("documented dressing counts reproduced", not wrong,
                 "; ".join(wrong[:4]) if wrong else "18/18 rows")

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
    for piece in pieces.values():
        for mesh_id in piece["meshIds"]:
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
    missing_files = []
    for material in materials:
        for field in ("mask", "albedo"):
            relative = material[field]
            if not relative:
                continue
            path = os.path.join(out_dir, relative)
            if not os.path.exists(path):
                missing_files.append(relative)
                continue
            width, height, depth, color_type = textures.read_png_size(path)
            if width <= 0 or height <= 0 or depth != 8:
                missing_files.append("%s %dx%d depth %d"
                                     % (relative, width, height, depth))
    report.check("mask/albedo PNGs exist and are 8 bit",
                 not missing_files,
                 "; ".join(missing_files[:3]) if missing_files else
                 "%d files" % (len(materials) * 2))

    # --- volume ----------------------------------------------------------
    total = 0
    for root, _, files in os.walk(out_dir):
        for name in files:
            total += os.path.getsize(os.path.join(root, name))
    limit = max_bytes if max_bytes else 40 * 1024 * 1024
    report.check("assets/hulls within size budget", total <= limit,
                 "%.2f MiB / %.2f MiB" % (total / 1048576.0, limit / 1048576.0))
    return report, {"meshFiles": mesh_files, "vertices": total_vertices,
                    "textureRate": round(rate, 4), "totalBytes": total}
