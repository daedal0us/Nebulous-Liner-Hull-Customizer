"""Unity type tree: node records, string resolution, tree shaping.

The common (global) string table is Unity's fixed list; per-type string buffers
hold the names Unity could not express there.  A node string offset with the
0x80000000 bit set indexes the common table (offset - 0x80000000), everything
else indexes the per-type buffer.

Serialized layout of one type entry (verified against stock-f2, see README.md):

    i32 classID | u8 isStripped | i16 scriptTypeIndex
    u8[16] oldTypeHash
    [u8[16] scriptID  -- only when classID == 114 (MonoBehaviour)]
    u32 nodeCount | u32 stringBufferSize
    nodeCount * node (32 B) | stringBufferSize bytes | u32 typeDependenciesCount
"""

from __future__ import annotations

import struct

COMMON_STRINGS = {
    0: "AABB", 5: "AnimationClip", 19: "AnimationCurve", 34: "AnimationState",
    49: "Array", 55: "Base", 60: "BitField", 69: "bitset", 76: "bool",
    81: "char", 86: "ColorRGBA", 96: "Component", 106: "data", 111: "deque",
    117: "double", 124: "dynamic_array", 138: "FastPropertyName", 155: "first",
    161: "float", 167: "Font", 172: "GameObject", 183: "Generic Mono",
    196: "GradientNEW", 208: "GUID", 213: "GUIStyle", 222: "int",
    226: "list", 231: "long long", 241: "map", 245: "Matrix4x4f",
    256: "MdFour", 263: "MonoBehaviour", 277: "MonoScript", 288: "m_ByteSize",
    299: "m_Curve", 307: "m_EditorClassIdentifier", 331: "m_EditorHideFlags",
    349: "m_Enabled", 359: "m_ExtensionPtr", 374: "m_GameObject",
    387: "m_Index", 395: "m_IsArray", 405: "m_IsStatic", 416: "m_MetaFlag",
    427: "m_Name", 434: "m_ObjectHideFlags", 452: "m_PrefabInternal",
    469: "m_PrefabParentObject", 490: "m_Script", 499: "m_StaticEditorFlags",
    519: "m_Type", 526: "m_Version", 536: "Object", 543: "pair",
    548: "PPtr<Component>", 564: "PPtr<GameObject>", 581: "PPtr<Material>",
    596: "PPtr<MonoBehaviour>", 616: "PPtr<MonoScript>", 633: "PPtr<Object>",
    646: "PPtr<Prefab>", 659: "PPtr<Sprite>", 672: "PPtr<TextAsset>",
    688: "PPtr<Texture>", 702: "PPtr<Texture2D>", 718: "PPtr<Transform>",
    734: "Prefab", 741: "Quaternionf", 753: "Rectf", 759: "RectInt",
    767: "RectOffset", 778: "second", 785: "set", 789: "short", 795: "size",
    800: "SInt16", 807: "SInt32", 814: "SInt64", 821: "SInt8",
    827: "staticvector", 840: "string", 847: "TextAsset", 857: "TextMesh",
    866: "Texture", 874: "Texture2D", 884: "Transform", 894: "TypelessData",
    907: "UInt16", 914: "UInt32", 921: "UInt64", 928: "UInt8",
    934: "unsigned int", 947: "unsigned long long", 966: "unsigned short",
    981: "vector", 988: "Vector2f", 997: "Vector3f", 1006: "Vector4f",
    1015: "m_ScriptingClassIdentifier", 1042: "Gradient", 1051: "Type*",
    1057: "int2_storage", 1070: "int3_storage", 1083: "BoundsInt",
    1093: "m_CorrespondingSourceObject", 1121: "m_PrefabInstance",
    1138: "m_PrefabAsset", 1152: "FileSize", 1161: "Hash128",
}

META_ALIGN = 0x4000
META_ANY_CHILD_ALIGN = 0x8000

#: Node type whose first child is an int element count and second the elements.
ARRAY_TYPES = frozenset(("Array",))

#: Unity 2020 wraps every array in one of these, with a single "Array" child.
ARRAY_WRAPPERS = frozenset(("vector", "staticvector", "dynamic_array", "map"))

FLOAT_TYPES = frozenset(("float", "double"))

UNSIGNED_TYPES = frozenset((
    "UInt8", "UInt16", "UInt32", "UInt64", "unsigned int",
    "unsigned short", "unsigned long long", "char", "FileSize"))

VARIABLE_TYPES = frozenset(("string", "TypelessData"))


def resolve_string(offset, string_buffer):
    """Resolve a type tree string offset (common table or per-type buffer)."""
    if offset & 0x80000000:
        index = offset - 0x80000000
        name = COMMON_STRINGS.get(index)
        if name is None:
            raise KeyError("unknown common string offset %d" % index)
        return name
    if offset >= len(string_buffer):
        raise KeyError("string offset %d outside buffer of %d bytes"
                       % (offset, len(string_buffer)))
    end = string_buffer.find(b"\x00", offset)
    if end < 0:
        end = len(string_buffer)
    return string_buffer[offset:end].decode("utf-8", "replace")


class TypeNode(object):
    __slots__ = ("version", "level", "type_flags", "type_offset", "name_offset",
                 "byte_size", "index", "meta_flag", "ref_type_hash",
                 "type_name", "name", "children", "parent")

    def __init__(self, version, level, type_flags, type_offset, name_offset,
                 byte_size, index, meta_flag, ref_type_hash):
        self.version = version
        self.level = level
        self.type_flags = type_flags
        self.type_offset = type_offset
        self.name_offset = name_offset
        self.byte_size = byte_size
        self.index = index
        self.meta_flag = meta_flag
        self.ref_type_hash = ref_type_hash
        self.type_name = ""
        self.name = ""
        self.children = []
        self.parent = None

    @property
    def is_array(self):
        if self.type_name in ARRAY_TYPES:
            return len(self.children) >= 2
        return (self.type_name in ARRAY_WRAPPERS and len(self.children) == 1
                and self.children[0].type_name in ARRAY_TYPES)

    @property
    def is_variable(self):
        return self.type_name in VARIABLE_TYPES or (not self.children
                                                   and self.byte_size < 0)

    def __repr__(self):
        return "TypeNode(%s %s, sz=%d, lvl=%d)" % (self.type_name, self.name,
                                                   self.byte_size, self.level)


class TypeTree(object):
    def __init__(self, nodes, string_buffer, type_dependencies=None):
        self.nodes = nodes
        self.string_buffer = string_buffer
        self.type_dependencies = type_dependencies or []
        self.roots = []
        self._shape()

    def _shape(self):
        stack = []
        for node in self.nodes:
            node.type_name = resolve_string(node.type_offset, self.string_buffer)
            node.name = resolve_string(node.name_offset, self.string_buffer)
            node.children = []
            while len(stack) > node.level:
                stack.pop()
            if node.level == 0:
                self.roots.append(node)
            else:
                if not stack:
                    raise ValueError("type tree level %d without parent" % node.level)
                node.parent = stack[-1]
                stack[-1].children.append(node)
            stack.append(node)

    @property
    def root(self):
        return self.roots[0] if self.roots else None

    def find(self, path):
        """Look a node up by dotted name path, e.g. m_VertexData.m_VertexCount."""
        parts = path.split(".")
        node = self.root
        for part in parts:
            for child in node.children:
                if child.name == part:
                    node = child
                    break
            else:
                return None
        return node

    def __len__(self):
        return len(self.nodes)

    def __repr__(self):
        return "<TypeTree %d nodes root=%r>" % (len(self.nodes),
                                                self.root.name if self.root else None)


_STRUCTS = {
    1: "<B", 2: "<h", 4: "<i", 8: "<q",
}
_UNSIGNED = {
    1: "<B", 2: "<H", 4: "<I", 8: "<Q",
}
_FLOATS = {4: "<f", 8: "<d"}


class Reader(object):
    """Little endian cursor over a bytes buffer."""

    __slots__ = ("data", "pos", "end")

    def __init__(self, data, pos=0, end=None):
        self.data = data
        self.pos = pos
        self.end = len(data) if end is None else end

    def read(self, size):
        pos = self.pos
        if pos + size > self.end:
            raise ValueError("read of %d bytes at %d past end %d"
                             % (size, pos, self.end))
        self.pos = pos + size
        return self.data[pos:pos + size]

    def unpack(self, fmt, size):
        pos = self.pos
        if pos + size > self.end:
            raise ValueError("read of %d bytes at %d past end %d"
                             % (size, pos, self.end))
        self.pos = pos + size
        return struct.unpack_from(fmt, self.data, pos)[0]

    def u8(self):
        return self.unpack("<B", 1)

    def i8(self):
        return self.unpack("<b", 1)

    def u16(self):
        return self.unpack("<H", 2)

    def i16(self):
        return self.unpack("<h", 2)

    def u32(self):
        return self.unpack("<I", 4)

    def i32(self):
        return self.unpack("<i", 4)

    def u64(self):
        return self.unpack("<Q", 8)

    def i64(self):
        return self.unpack("<q", 8)

    def f32(self):
        return self.unpack("<f", 4)

    def align(self, alignment=4):
        remainder = self.pos % alignment
        if remainder:
            self.pos += alignment - remainder

    def cstring(self):
        end = self.data.find(b"\x00", self.pos, self.end)
        if end < 0:
            raise ValueError("unterminated string at %d" % self.pos)
        text = self.data[self.pos:end].decode("utf-8", "replace")
        self.pos = end + 1
        return text


def read_primitive(reader, node):
    size = node.byte_size
    name = node.type_name
    if name in FLOAT_TYPES and size in _FLOATS:
        return reader.unpack(_FLOATS[size], size)
    if size in (1, 2, 4, 8):
        fmt = _UNSIGNED.get(size) if name in UNSIGNED_TYPES else _STRUCTS.get(size)
        if fmt is None:
            return reader.read(size)
        return reader.unpack(fmt, size)
    return reader.read(size)


def read_value(reader, node):
    """Deserialize one value described by node into plain Python data."""
    children = node.children
    if children and node.type_name in ARRAY_WRAPPERS and len(children) == 1:
        value = read_value(reader, children[0])
        if node.meta_flag & META_ALIGN:
            reader.align()
        return value
    if children and node.type_name in VARIABLE_TYPES:
        inner = children[0]
        inner_children = inner.children
        if inner_children:
            count = read_primitive(reader, inner_children[0])
            element = inner_children[1] if len(inner_children) > 1 else None
            width = element.byte_size if element is not None else 1
        else:
            count = read_primitive(reader, inner)
            width = 1
        raw = reader.read(count * width) if count else b""
        if inner.meta_flag & META_ALIGN or node.meta_flag & META_ALIGN:
            reader.align()
        if node.type_name == "string":
            return raw.decode("utf-8", "replace")
        return raw
    if children and node.type_name in ARRAY_TYPES:
        count_node = children[0]
        data_node = children[1] if len(children) > 1 else None
        count = read_primitive(reader, count_node)
        values = []
        if data_node is not None and count:
            if data_node.children or data_node.is_variable:
                for _ in range(count):
                    values.append(read_value(reader, data_node))
            else:
                size = data_node.byte_size
                if size > 0:
                    raw = reader.read(size * count)
                    fast = _bulk_unpack(raw, data_node, count)
                    values = fast if fast is not None else list(raw)
        if node.meta_flag & META_ALIGN:
            reader.align()
        return values
    if children:
        result = {}
        for child in children:
            result[child.name] = read_value(reader, child)
        if node.meta_flag & META_ALIGN:
            reader.align()
        return result
    if node.is_variable:
        size = reader.i32()
        raw = reader.read(size) if size else b""
        reader.align()
        if node.type_name == "string":
            return raw.decode("utf-8", "replace")
        return raw
    value = read_primitive(reader, node)
    if node.meta_flag & META_ALIGN:
        reader.align()
    return value


def _bulk_unpack(raw, node, count):
    size = node.byte_size
    name = node.type_name
    if name in FLOAT_TYPES and size in _FLOATS:
        fmt = _FLOATS[size]
    elif name in UNSIGNED_TYPES and size in _UNSIGNED:
        fmt = _UNSIGNED[size]
    elif size in _STRUCTS:
        fmt = _STRUCTS[size]
    else:
        return None
    return list(struct.unpack("<%d%s" % (count, fmt[-1]), raw))


def read_type_tree(reader, tree):
    """Deserialize a whole type tree, returning a dict of top level fields."""
    return read_value(reader, tree.root)
