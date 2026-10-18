"""Rebuilds the GameObject/Transform graph of one serialized file and exposes
the modular hull prefabs.

Nothing here is positional: every value comes from the type tree driven
deserializer, looked up by field name.
"""

from __future__ import annotations

import struct

CLASS_GAMEOBJECT = 1
CLASS_TRANSFORM = 4
CLASS_RECTTRANSFORM = 224
CLASS_MESH_FILTER = 33
CLASS_MESH_RENDERER = 23
CLASS_MONOBEHAVIOUR = 114

#: hull saveKey -> (role, container path in the asset bundle)
HULL_PREFABS = {
    "Stock/Bulk Hauler": (
        "marauder",
        "assets/#bundles/stock-f2/civ/hulls/bulk hauler.prefab",
        "Bulk Hauler"),
    "Stock/Container Hauler": (
        "moorline",
        "assets/#bundles/stock-f2/f2/hulls/container line ship.prefab",
        "Container Hauler"),
    "Stock/Container Hauler Refit": (
        "moorline-refit",
        "assets/#bundles/stock-f2/f2/hulls/container line ship refit.prefab",
        "Container Hauler Refit"),
}


def pptr_path_id(value):
    """Extract a pathID from a PPtr dict (0 when the reference is null)."""
    if isinstance(value, dict):
        path_id = value.get("m_PathID", 0)
        return path_id if path_id else 0
    return 0


def pptr_list(value):
    if not value:
        return []
    result = []
    for item in value:
        path_id = pptr_path_id(item)
        if path_id:
            result.append(path_id)
    return result


def vec3(value, default=(0.0, 0.0, 0.0)):
    if isinstance(value, dict):
        return (float(value.get("x", default[0])),
                float(value.get("y", default[1])),
                float(value.get("z", default[2])))
    return default


def quat(value):
    if isinstance(value, dict):
        return (float(value.get("x", 0.0)), float(value.get("y", 0.0)),
                float(value.get("z", 0.0)), float(value.get("w", 1.0)))
    return (0.0, 0.0, 0.0, 1.0)


class SceneGraph(object):
    """GameObject hierarchy of a serialized file, keyed by pathID."""

    def __init__(self, serialized_file):
        self.sf = serialized_file
        self._values = {}
        self.go_names = {}
        self.go_components = {}
        self.component_owner = {}
        self.transform_of_go = {}
        self.go_of_transform = {}
        self.tr_father = {}
        self.tr_children = {}
        self.tr_local = {}
        self.component_class = {}
        self._load()

    # -- raw access --------------------------------------------------------
    def value(self, path_id):
        cached = self._values.get(path_id)
        if cached is None:
            info = self.sf.by_path.get(path_id)
            cached = self.sf.deserialize(info) if info is not None else {}
            self._values[path_id] = cached
        return cached

    def _load(self):
        sf = self.sf
        for info in sf.objects_of(CLASS_GAMEOBJECT):
            value = self.value(info.path_id)
            self.go_names[info.path_id] = value.get("m_Name", "")
            components = []
            for item in value.get("m_Component") or ():
                comp = item.get("component") if isinstance(item, dict) else None
                path_id = pptr_path_id(comp)
                if path_id:
                    components.append(path_id)
                    self.component_owner[path_id] = info.path_id
            self.go_components[info.path_id] = components

        for class_id in (CLASS_TRANSFORM, CLASS_RECTTRANSFORM):
            for info in sf.objects_of(class_id):
                value = self.value(info.path_id)
                go = pptr_path_id(value.get("m_GameObject"))
                if not go:
                    continue
                self.go_of_transform[info.path_id] = go
                self.transform_of_go[go] = info.path_id
                self.tr_father[info.path_id] = pptr_path_id(value.get("m_Father"))
                self.tr_children[info.path_id] = [
                    c for c in pptr_list(value.get("m_Children"))]
                self.tr_local[info.path_id] = (
                    vec3(value.get("m_LocalPosition")),
                    quat(value.get("m_LocalRotation")),
                    vec3(value.get("m_LocalScale"), (1.0, 1.0, 1.0)))

        scripts = sf.mono_scripts()
        for component, script in sf.component_script_path_ids().items():
            name = scripts.get(script)
            if name:
                self.component_class[component] = name

    # -- navigation --------------------------------------------------------
    def name_of(self, go):
        return self.go_names.get(go, "")

    def parent_of(self, go):
        transform = self.transform_of_go.get(go)
        if transform is None:
            return 0
        father = self.tr_father.get(transform, 0)
        return self.go_of_transform.get(father, 0) if father else 0

    def children_of(self, go):
        transform = self.transform_of_go.get(go)
        if transform is None:
            return []
        return [self.go_of_transform[c] for c in self.tr_children.get(transform, ())
                if c in self.go_of_transform]

    def local_transform(self, go):
        transform = self.transform_of_go.get(go)
        if transform is None:
            return ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))
        return self.tr_local[transform]

    def walk(self, root_go):
        """Yield (depth, gameObject) in Unity's preorder."""
        stack = [(0, root_go)]
        seen = set()
        while stack:
            depth, go = stack.pop()
            if go in seen:
                continue
            seen.add(go)
            yield depth, go
            children = self.children_of(go)
            for child in reversed(children):
                stack.append((depth + 1, child))

    def components_of_class(self, go, class_name):
        result = []
        for component in self.go_components.get(go, ()):
            if self.component_class.get(component) == class_name:
                result.append(component)
        return result

    def components_by_class_id(self, go, class_id):
        result = []
        for component in self.go_components.get(go, ()):
            info = self.sf.by_path.get(component)
            if info is not None and info.class_id == class_id:
                result.append(component)
        return result

    def find_component_in_subtree(self, root_go, class_name):
        for _, go in self.walk(root_go):
            found = self.components_of_class(go, class_name)
            if found:
                return go, found[0]
        return 0, 0

    # -- asset bundle container -------------------------------------------
    def container(self):
        """asset path (lower case) -> GameObject pathID."""
        if getattr(self, "_container", None) is None:
            table = {}
            for info in self.sf.objects_of(142):
                for entry in self.value(info.path_id).get("m_Container") or ():
                    if not isinstance(entry, dict):
                        continue
                    path = entry.get("first")
                    second = entry.get("second") or {}
                    asset = pptr_path_id(second.get("asset"))
                    if isinstance(path, str) and asset:
                        table[path] = asset
            self._container = table
        return self._container

    def prefab_root(self, container_path):
        return self.container().get(container_path.lower(), 0)


def world_transform(graph, go, cache=None):
    """Section space position/quaternion/scale of a GameObject."""
    if cache is not None and go in cache:
        return cache[go]
    chain = []
    current = go
    while current:
        chain.append(current)
        current = graph.parent_of(current)
    position = (0.0, 0.0, 0.0)
    rotation = (0.0, 0.0, 0.0, 1.0)
    scale = (1.0, 1.0, 1.0)
    for game_object in reversed(chain):
        local_position, local_rotation, local_scale = graph.local_transform(game_object)
        position, rotation, scale = _compose(position, rotation, scale,
                                             local_position, local_rotation,
                                             local_scale)
    result = (position, rotation, scale)
    if cache is not None:
        cache[go] = result
    return result


def _compose(parent_position, parent_rotation, parent_scale,
             local_position, local_rotation, local_scale):
    from . import mathutil
    scaled = (local_position[0] * parent_scale[0],
              local_position[1] * parent_scale[1],
              local_position[2] * parent_scale[2])
    rotated = mathutil.quat_rotate(parent_rotation, scaled)
    position = (parent_position[0] + rotated[0],
                parent_position[1] + rotated[1],
                parent_position[2] + rotated[2])
    rotation = mathutil.quat_mul(parent_rotation, local_rotation)
    scale = (parent_scale[0] * local_scale[0],
             parent_scale[1] * local_scale[1],
             parent_scale[2] * local_scale[2])
    return position, rotation, scale


class DressingPoint(object):
    __slots__ = ("index", "allow_empty", "variant_roots", "game_object", "name")

    def __init__(self, index, allow_empty, variant_roots, game_object, name):
        self.index = index
        self.allow_empty = allow_empty
        self.variant_roots = variant_roots
        self.game_object = game_object
        self.name = name

    def __repr__(self):
        return "DressingPoint(%d, allowEmpty=%s, %d variants)" % (
            self.index, self.allow_empty, len(self.variant_roots))


class Section(object):
    """One modular hull section prefab (bow / core / stern / superstructure)."""

    def __init__(self, root_go, kind, key, debug_name, component):
        self.root_go = root_go
        self.kind = kind
        self.key = key
        self.debug_name = debug_name
        self.component = component
        self.fore_snap_go = 0
        self.aft_snap_go = 0
        self.superstructure_snap_gos = []
        self.dressing_points = []
        self.fore_snap = None
        self.aft_snap = None
        self.superstructure_snaps = []

    @property
    def id(self):
        return "p:" + self.key.split("-")[0][:8] if self.key else "p:?"

    def __repr__(self):
        return "Section(%s %s %r %d dressing points)" % (
            self.kind, self.id, self.debug_name, len(self.dressing_points))


class HullType(object):
    def __init__(self, save_key, role, display_name, prefab_path, root_go, component):
        self.save_key = save_key
        self.role = role
        self.display_name = display_name
        self.prefab_path = prefab_path
        self.root_go = root_go
        self.component = component
        self.bows = []
        self.cores = []
        self.sterns = []
        self.superstructures = []


def _prefab_name(graph, root_go):
    """Human name of a prefab: its container file name, else the GameObject name."""
    for path, path_id in graph.container().items():
        if path_id == root_go:
            return path.rsplit("/", 1)[-1].rsplit(".", 1)[0].title()
    return graph.name_of(root_go)


def section_from_component(graph, component):
    """Build a Section from a primary or secondary structure component."""
    value = graph.value(component)
    script = graph.component_class.get(component, "")
    root_go = pptr_path_id(value.get("m_GameObject"))
    if script == "ModularHullSecondaryStructure":
        section = Section(root_go, "superstructure", value.get("_key", ""),
                          _prefab_name(graph, root_go), component)
        return section
    kind = _kind_of(graph, root_go)
    section = Section(root_go, kind, value.get("_key", ""),
                      _prefab_name(graph, root_go), component)
    section.fore_snap_go = pptr_path_id(value.get("_foreSnapPoint"))
    section.aft_snap_go = pptr_path_id(value.get("_aftSnapPoint"))
    section.superstructure_snap_gos = pptr_list(value.get("_superstructureSnap"))
    section.dressing_points = dressing_points(graph, root_go)
    return section


def _kind_of(graph, root_go):
    name = graph.name_of(root_go).lower()
    for kind in ("bow", "core", "stern"):
        if kind in name:
            return kind
    return "section"


def dressing_points(graph, root_go):
    """Dressing components under root_go, in preorder."""
    points = []
    for _, go in graph.walk(root_go):
        for component in graph.components_of_class(go, "ModularHullDressing"):
            value = graph.value(component)
            points.append(DressingPoint(
                len(points), bool(value.get("_allowEmpty", 0)),
                graph.children_of(go), go, graph.name_of(go)))
    return points


def all_sections(graph):
    """Every primary and secondary structure component in the file."""
    sections = []
    for component, script in sorted(graph.component_class.items()):
        if script in ("ModularHullPrimaryStructure",
                      "ModularHullSecondaryStructure"):
            sections.append(section_from_component(graph, component))
    return sections


def resolve_snaps(graph, section):
    """Fill in section space snap transforms (relative to the section root)."""
    if section.fore_snap_go:
        position, rotation, _ = world_transform(graph, section.fore_snap_go)
        section.fore_snap = (position, rotation)
    if section.aft_snap_go:
        position, rotation, _ = world_transform(graph, section.aft_snap_go)
        section.aft_snap = (position, rotation)
    section.superstructure_snaps = []
    for snap_go in section.superstructure_snap_gos:
        position, rotation, _ = world_transform(graph, snap_go)
        section.superstructure_snaps.append((position, rotation))
    return section


def load_hull_types(graph):
    """Locate the three ship prefabs and their section candidate lists."""
    result = {}
    for save_key, (role, container_path, display_name) in HULL_PREFABS.items():
        root_go = graph.prefab_root(container_path)
        if not root_go:
            raise LookupError("prefab %r not found in bundle container"
                              % (container_path,))
        _, component = graph.find_component_in_subtree(root_go, "RandomModularHull")
        if not component:
            raise LookupError("RandomModularHull missing on %r" % (container_path,))
        value = graph.value(component)
        hull = HullType(save_key, role, display_name, container_path, root_go,
                        component)
        for attribute, field in (("bows", "_bows"), ("cores", "_cores"),
                                 ("sterns", "_sterns"),
                                 ("superstructures", "_superstructures")):
            sections = []
            for path_id in pptr_list(value.get(field)):
                sections.append(section_from_component(graph, path_id))
            sections.sort(key=lambda s: s.key)
            setattr(hull, attribute, sections)
        result[save_key] = hull
    return result
