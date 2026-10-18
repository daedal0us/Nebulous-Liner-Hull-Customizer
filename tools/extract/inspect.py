"""Read-only forensic dumps behind a catalog entry.

Nothing here writes an asset or a catalog field: it opens the same bundle the
extractor opens and prints the raw material / node / texture facts, so a
suspicious render can be traced to game data instead of guessed at.

    python -m tools.extract.inspect --bundle <stock-f2> \
        --hhull "Stock/Bulk Hauler" --nodes
    python -m tools.extract.inspect --bundle <stock-f2> --decal-slots
    python -m tools.extract.inspect --bundle <stock-f2> \
        --material "Lineship Doodads" --colors
    python -m tools.extract.inspect --bundle <stock-f2> \
        --hull "Stock/Bulk Hauler" --group superstructures --nested
    python -m tools.extract.inspect --bundle <stock-f2> \
        --material "Superstructure" --shaders
"""

from __future__ import annotations

import argparse
import re
import sys

from . import export, hierarchy, mathutil, meshes, serialized, textures, unityfs

#: Unity classes that carry a nested prefab instance: one instance object names
#: the parent transform and the source prefab, because the instance's own
#: GameObjects are cloned at load time and never serialized in this file.
CLASS_PREFAB_INSTANCE = 1001
CLASS_PREFAB = 1002


def open_scene(bundle_path):
    bundle = unityfs.open_bundle(bundle_path)
    sf = serialized.open_from_bundle(bundle)
    return bundle, hierarchy.SceneGraph(sf)


def material_object(graph, path_id):
    return graph.value(path_id) or {} if path_id else {}


def material_name(graph, path_id):
    return material_object(graph, path_id).get("m_Name") or ""


def texenvs(graph, path_id):
    """{property: texture pathID} of a material."""
    properties = material_object(graph, path_id).get("m_SavedProperties") or {}
    found = {}
    for entry in properties.get("m_TexEnvs") or ():
        if not isinstance(entry, dict):
            continue
        texture = hierarchy.pptr_path_id((entry.get("second") or {}).get("m_Texture"))
        if entry.get("first") and texture:
            found[entry["first"]] = texture
    return found


def saved_properties(graph, path_id, field):
    """[(name, value)] of one m_SavedProperties array (m_Colors / m_Floats)."""
    properties = material_object(graph, path_id).get("m_SavedProperties") or {}
    found = []
    for entry in properties.get(field) or ():
        if not isinstance(entry, dict):
            continue
        value = entry.get("second")
        if field == "m_Colors":
            value = value or {}
            value = (value.get("r", 0.0), value.get("g", 0.0),
                     value.get("b", 0.0), value.get("a", 0.0))
        found.append((entry.get("first"), value))
    return found


def livery_roles(graph, hulls):
    """material pathID -> the role export.MaterialBuilder assigns."""
    livery, _segments, _unresolved, _skipped, _unmasked = \
        export.livery_materials(graph, hulls)
    return {path_id: "livery" for path_id in livery}


def node_rows(graph, section, mesh_cache):
    """The export.PieceBuilder.build traversal, without writing any mesh."""
    rows = []
    node_of_go = {}
    for depth, go in graph.walk(section.root_go):
        parent = graph.parent_of(go)
        row = {"index": len(rows),
               "parent": node_of_go.get(parent, -1) if depth else -1,
               "name": graph.name_of(go), "mesh": "", "submesh": -1,
               "submeshes": 0, "slots": 0, "material": None}
        node_of_go[go] = len(rows)
        rows.append(row)
        filters = graph.components_by_class_id(go, hierarchy.CLASS_MESH_FILTER)
        if not filters:
            continue
        mesh_id = hierarchy.pptr_path_id(graph.value(filters[0]).get("m_Mesh"))
        if not mesh_id:
            continue
        renderers = graph.components_by_class_id(go, hierarchy.CLASS_MESH_RENDERER)
        material_paths = []
        if renderers:
            for material in graph.value(renderers[0]).get("m_Materials") or ():
                path_id = hierarchy.pptr_path_id(material)
                if path_id:
                    material_paths.append(path_id)
        try:
            mesh = meshes.read_mesh(graph.sf, mesh_id, mesh_cache)
        except Exception as exc:
            print("    mesh %d on %r skipped: %s" % (mesh_id, row["name"], exc),
                  file=sys.stderr)
            continue
        submeshes = mesh.submeshes or [(0, len(mesh.indices), 0, 0, 0,
                                        mesh.vertex_count)]
        for index, _submesh in enumerate(submeshes):
            material = (material_paths[min(index, len(material_paths) - 1)]
                        if material_paths else None)
            if index == 0:
                target = row
            else:
                target = {"index": len(rows), "parent": node_of_go[go],
                          "name": "%s#%d" % (row["name"], index), "mesh": "",
                          "submesh": -1, "submeshes": 0, "slots": 0,
                          "material": None}
                rows.append(target)
            target["mesh"] = mesh.name
            target["submesh"] = index
            target["submeshes"] = len(submeshes)
            target["slots"] = len(material_paths)
            target["material"] = material
    return rows


def shader_properties(graph, path_id):
    """(shader name, property names the shader really declares) of a material.

    A material keeps the m_Colors / m_TexEnvs entries of a shader it no longer
    uses, so a leftover "_BasePaintTint" is no evidence that anything paints:
    only m_ParsedForm.m_PropInfo lists the uniforms the shader actually has.
    """
    material = material_object(graph, path_id)
    shader_id = hierarchy.pptr_path_id(material.get("m_Shader"))
    if not shader_id:
        return "", frozenset()
    parsed = graph.value(shader_id).get("m_ParsedForm") or {}
    props = (parsed.get("m_PropInfo") or {}).get("m_Props") or ()
    return (parsed.get("m_Name") or "",
            frozenset(item.get("m_Name") for item in props
                      if isinstance(item, dict)))


def declares(graph, path_id, field, wanted):
    """True when one m_SavedProperties array of a material holds a property."""
    properties = material_object(graph, path_id).get("m_SavedProperties") or {}
    for entry in properties.get(field) or ():
        if isinstance(entry, dict) and entry.get("first") == wanted:
            return True
    return False


def nested_instances(graph):
    """[(parent transform pathID, prefab asset path, root GameObject pathID)].

    A nested prefab instance serializes no GameObjects of its own: one
    PrefabInstance object names m_TransformParent plus the Prefab it clones,
    whose m_Name is the asset path it came from (empty when the hierarchy lives
    in this file, which m_RootGameObject then points at).
    """
    found = []
    for info in graph.sf.objects_of(CLASS_PREFAB_INSTANCE):
        value = graph.value(info.path_id)
        prefab_id = hierarchy.pptr_path_id(value.get("m_SourcePrefab"))
        path, root = "", 0
        if prefab_id:
            prefab = graph.value(prefab_id)
            path = prefab.get("m_Name") or ""
            root = hierarchy.pptr_path_id(prefab.get("m_RootGameObject"))
        found.append((hierarchy.pptr_path_id(value.get("m_TransformParent")),
                      path, root))
    return found


def y_range(mesh, position, rotation, scale):
    """(min, max) Y of a mesh's vertices under one transform, or None."""
    if not mesh.positions:
        return None
    values = [mathutil.transform_point(position, rotation, scale, point)[1]
              for point in mesh.positions]
    return min(values), max(values)


def dump_nested(graph, hull, group, stream, mesh_cache):
    """Every node of a group, its mesh / material / paint role and Y extent.

    Nested prefab instances are reported as their own row (with the prefab asset
    path) instead of being silently dropped by the transform walk.
    """
    instances = nested_instances(graph)
    roles = livery_roles(graph, {hull.save_key: hull})
    for section in getattr(hull, group):
        space = hierarchy.SectionSpace(graph, section.root_go)
        rows = list(graph.walk(section.root_go))
        transforms = set()
        print("\n-- section %s %r (%s) nodes=%d nested instances in file=%d"
              % (section.id, section.debug_name, section.kind, len(rows),
                 len(instances)), file=stream)
        for depth, go in rows:
            transforms.add(graph.transform_of_go.get(go, 0))
            filters = graph.components_by_class_id(go, hierarchy.CLASS_MESH_FILTER)
            mesh = None
            material = None
            if filters:
                mesh_id = hierarchy.pptr_path_id(
                    graph.value(filters[0]).get("m_Mesh"))
                if mesh_id:
                    try:
                        mesh = meshes.read_mesh(graph.sf, mesh_id, mesh_cache)
                    except Exception as exc:
                        print("    mesh %d on %r skipped: %s"
                              % (mesh_id, graph.name_of(go), exc), file=stream)
                renderers = graph.components_by_class_id(
                    go, hierarchy.CLASS_MESH_RENDERER)
                if renderers:
                    used = graph.value(renderers[0]).get("m_Materials") or ()
                    material = (hierarchy.pptr_path_id(used[0]) if used else 0) or None
            shader, props = shader_properties(graph, material) if material else ("", frozenset())
            local = space.transform_of_game_object(graph, go)
            world = hierarchy.world_transform(graph, go)
            spans = []
            for label, transform in (("yS", local), ("yW", world)):
                span = y_range(mesh, *transform) if mesh else None
                spans.append("%s=[%7.2f,%6.2f]" % (label, span[0], span[1])
                             if span else "%s=?        " % label)
            print("   %s%-26r mesh=%-28r mat=%-26r %s %s tint=%-3s shaderTint=%-3s "
                  "shaderMask=%-3s shader=%r"
                  % ("  " * depth, graph.name_of(go),
                     mesh.name if mesh else "-",
                     material_name(graph, material) if material else "-",
                     spans[0], spans[1],
                     "yes" if material and declares(graph, material, "m_Colors",
                                                    "_BasePaintTint") else "no",
                     "yes" if "_BasePaintTint" in props else "no",
                     "yes" if "_PaintMask" in props else "no",
                     shader), file=stream)
        for parent, path, root in instances:
            if parent not in transforms:
                continue
            stored = graph.name_of(root) if root else ""
            print("   %-26r mesh=%s mat=%s prefab=%r %s"
                  % ("(nested prefab)", "-", "-", path,
                     "stored root %r" % stored if stored
                     else "external: contents are not serialized here"),
                  file=stream)


def dump_shaders(graph, pattern, stream):
    """Per material: shader name and the paint uniforms that shader declares."""
    matcher = re.compile(pattern, re.IGNORECASE)
    shown = 0
    for info in graph.sf.objects_of(export.CLASS_MATERIAL):
        name = material_name(graph, info.path_id)
        if not matcher.search(name):
            continue
        shown += 1
        shader, props = shader_properties(graph, info.path_id)
        declared = [item for item in ("_BasePaintTint", "_StripePaintTint",
                                      "_PaintMask", "_HullTint")
                    if item in props]
        stale = [item for item in ("_BasePaintTint", "_StripePaintTint")
                 if declares(graph, info.path_id, "m_Colors", item)]
        print("%-34r %-38r paints=%-52s leftover=%s"
              % (name, shader, ",".join(declared) or "-",
                 ",".join(stale) or "-"), file=stream)
    print("\n%d matching materials" % shown, file=stream)


def describe(graph, path_id, roles):
    if path_id is None:
        return "(no material)"
    properties = texenvs(graph, path_id)
    decals = [name for name in ("_nameplateDecal", "_DecalMap")
              if name in properties]
    albedo = properties.get("_BaseColorMap") or properties.get("_MainTex")
    return ("%-20d %-34r role=%-6s _PaintMask=%-3s decal=%-17s albedo=%r"
            % (path_id, material_name(graph, path_id),
               roles.get(path_id, "fixed"),
               "yes" if "_PaintMask" in properties else "no",
               ",".join(decals) if decals else "no",
               material_name(graph, albedo) if albedo else "-"))


def channel_stats(channels, names=("r", "g", "b", "a")):
    """mean / min / max / 5-25-50-75-95 percentiles per channel."""
    count = float(len(channels[0]))
    result = {}
    for name, channel in zip(names, channels):
        histogram = [0] * 256
        for value in channel:
            histogram[value] += 1
        mean = sum(value * hits for value, hits in enumerate(histogram)) / count
        quantiles = []
        target = 0
        for wanted in (0.05, 0.25, 0.5, 0.75, 0.95):
            goal = wanted * count
            while target < 255 and sum(histogram[:target + 1]) < goal:
                target += 1
            quantiles.append(target)
        result[name] = (mean, min(channel), max(channel), quantiles)
    return result


def dump_texture(graph, path_id, stream, size=1024):
    stream_container = unityfs.resource_stream
    del stream_container
    info = textures.read_texture(graph.sf, path_id)
    level, _offset, width, height = textures.pick_mip(info, size)
    _w, _h, red, green, blue, alpha = textures.decode_mip(
        info, level, graph.resource_stream)
    stats = channel_stats((red, green, blue, alpha))
    print("  texture %r %dx%d %s mip%d %dx%d"
          % (info.name, info.width, info.height, info.format_name, level,
             width, height), file=stream)
    for name in ("r", "g", "b", "a"):
        mean, low, high, quantiles = stats[name]
        print("    %s mean %6.2f min %3d max %3d  p5 %3d p25 %3d p50 %3d "
              "p75 %3d p95 %3d"
              % (name, mean, low, high, quantiles[0], quantiles[1],
                 quantiles[2], quantiles[3], quantiles[4]), file=stream)
    print("    grayscale(within 6 levels) = %s"
          % textures.is_grayscale(red, green, blue), file=stream)


def dump_colors(graph, pattern, stream, floats=False):
    matcher = re.compile(pattern, re.IGNORECASE)
    shown = 0
    for info in graph.sf.objects_of(export.CLASS_MATERIAL):
        name = material_name(graph, info.path_id)
        if not matcher.search(name):
            continue
        shown += 1
        shader = hierarchy.pptr_path_id(
            material_object(graph, info.path_id).get("m_Shader"))
        print("\n=== material %r pathID %d shader=%r ==="
              % (name, info.path_id,
                 material_name(graph, shader) if shader else "?"), file=stream)
        for property_name, value in saved_properties(graph, info.path_id,
                                                     "m_Colors"):
            print("  m_Colors %-24s = (%.6f, %.6f, %.6f, %.6f)"
                  % ((property_name,) + value), file=stream)
        if floats:
            for property_name, value in saved_properties(graph, info.path_id,
                                                         "m_Floats"):
                print("  m_Floats %-24s = %s" % (property_name, value),
                      file=stream)
        properties = texenvs(graph, info.path_id)
        for property_name, texture in sorted(properties.items()):
            print("  m_TexEnvs %-23s -> %r"
                  % (property_name, material_name(graph, texture)), file=stream)
        albedo = properties.get("_BaseColorMap") or properties.get("_MainTex")
        if albedo:
            dump_texture(graph, albedo, stream)
    print("\n%d matching materials" % shown, file=stream)


def dump_decal_slots(graph, stream):
    found = 0
    for info in graph.sf.objects_of(export.CLASS_MATERIAL):
        properties = texenvs(graph, info.path_id)
        names = [name for name in ("_nameplateDecal", "_DecalMap")
                 if name in properties]
        if not names:
            continue
        found += 1
        for property_name in names:
            try:
                texture = textures.read_texture(graph.sf, properties[property_name])
                detail = "%r %dx%d %s mips=%d stream=%d" % (
                    texture.name, texture.width, texture.height,
                    texture.format_name, texture.mip_count, texture.stream_size)
            except Exception as exc:
                detail = "undecodable: %s" % exc
            print("%-40s %-17s %s" % (material_name(graph, info.path_id),
                                      property_name, detail), file=stream)
    print("\n%d materials declare a nameplate decal" % found, file=stream)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python -m tools.extract.inspect")
    parser.add_argument("--bundle", required=True)
    parser.add_argument("--hull", default="Stock/Bulk Hauler",
                        help="hull save key for --nodes")
    parser.add_argument("--group", default="superstructures",
                        help="bows / cores / sterns / superstructures")
    parser.add_argument("--nodes", action="store_true",
                        help="print every node of the group with its material")
    parser.add_argument("--decal-slots", action="store_true",
                        help="print every material that declares a decal")
    parser.add_argument("--nested", action="store_true",
                        help="every node of the group with mesh, material, "
                             "paint uniforms and Y extent (nested prefab "
                             "instances are listed, not dropped)")
    parser.add_argument("--shaders", action="store_true",
                        help="per material: shader name and the paint uniforms "
                             "that shader declares (vs leftover m_Colors)")
    parser.add_argument("--material", default=".", help="material name regex")
    parser.add_argument("--colors", action="store_true",
                        help="print m_Colors (and m_Floats) + albedo statistics")
    parser.add_argument("--floats", action="store_true",
                        help="include the ~100 m_Floats entries in --colors")
    args = parser.parse_args(argv)
    stream = sys.stdout
    bundle, graph = open_scene(args.bundle)
    try:
        graph.resource_stream = unityfs.resource_stream(bundle)
        hulls = hierarchy.load_hull_types(graph)
        if args.decal_slots:
            print("=== materials declaring a nameplate decal ===", file=stream)
            dump_decal_slots(graph, stream)
        if args.nodes:
            roles = livery_roles(graph, hulls)
            hull = hulls[args.hull]
            sections = getattr(hull, args.group)
            print("\n=== %s %s: %d sections ==="
                  % (args.hull, args.group, len(sections)), file=stream)
            mesh_cache = {}
            for section in sections:
                rows = node_rows(graph, section, mesh_cache)
                print("\n-- section %s %r (%s) nodes=%d"
                      % (section.id, section.debug_name, section.kind,
                         len(rows)), file=stream)
                for row in rows:
                    print("   node %3d par %3d %-40r mesh=%-28r sub=%d/%d "
                          "slots=%d %s"
                          % (row["index"], row["parent"], row["name"],
                             row["mesh"] or "-", row["submesh"],
                             row["submeshes"] - 1, row["slots"],
                             describe(graph, row["material"], roles)),
                          file=stream)
        if args.nested:
            hull = hulls[args.hull]
            print("\n=== %s %s: node table with paint uniforms ==="
                  % (args.hull, args.group), file=stream)
            dump_nested(graph, hull, args.group, stream, {})
        if args.shaders:
            print("\n=== material -> shader paint uniforms ===", file=stream)
            dump_shaders(graph, args.material, stream)
        if args.colors:
            dump_colors(graph, args.material, stream, floats=args.floats)
    finally:
        bundle.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
