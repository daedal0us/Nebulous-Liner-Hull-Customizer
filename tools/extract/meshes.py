"""Mesh extraction: m_VertexData channels -> positions/normals/uv0,
m_IndexBuffer -> indices, then baking into section space and .nlmesh output.

Field access is type tree driven (see typetree.py); nothing is hard coded to an
offset.  Verified against stock-f2: all 734 meshes are m_MeshCompression == 0
(uncompressed vertex buffers) and m_IndexFormat == 0 (UInt16 indices).
"""

from __future__ import annotations

import array
import struct

from . import typetree

ATTRIBUTE_POSITION = 0
ATTRIBUTE_NORMAL = 1
ATTRIBUTE_TEXCOORD0 = 4

#: VertexAttributeFormat -> (struct code, scale or None for integer formats)
FORMATS = {
    0: ("f", 4, None),
    1: ("e", 2, None),
    2: ("B", 1, 255.0),
    3: ("b", 1, 127.0),
    4: ("H", 2, 65535.0),
    5: ("h", 2, 32767.0),
    6: ("B", 1, None),
    7: ("b", 1, None),
    8: ("H", 2, None),
    9: ("h", 2, None),
    10: ("I", 4, None),
    11: ("i", 4, None),
}

NLHM_MAGIC = b"NLHM"
NLHM_VERSION = 1
FLAG_NORMALS = 1
FLAG_UV0 = 2
FLAG_INDEX32 = 4
FLAG_TRIM = 8


class MeshError(Exception):
    pass


class MeshData(object):
    __slots__ = ("name", "vertex_count", "positions", "normals", "uvs",
                 "indices", "submeshes", "index_format", "compression")

    def __init__(self):
        self.name = ""
        self.vertex_count = 0
        self.positions = []
        self.normals = []
        self.uvs = []
        self.indices = []
        self.submeshes = []
        self.index_format = 0
        self.compression = 0

    def submesh_indices(self, submesh):
        first_byte, count = submesh[0], submesh[1]
        width = 4 if self.index_format == 1 else 2
        start = first_byte // width
        return self.indices[start:start + count]

    def submesh_vertex_range(self, submesh):
        first_vertex, vertex_count = submesh[4], submesh[5]
        return first_vertex, first_vertex + vertex_count


def read_mesh(serialized_file, path_id, cache=None):
    """Decode a Mesh object into MeshData (cached by pathID)."""
    if cache is not None and path_id in cache:
        return cache[path_id]
    info = serialized_file.by_path.get(path_id)
    if info is None:
        raise MeshError("mesh %d not found" % path_id)
    tree = serialized_file.types[info.type_id].tree
    if tree is None:
        raise MeshError("mesh %d has no type tree" % path_id)
    reader = serialized_file.reader(info)
    fields = read_mesh_fields(reader, tree)
    mesh = MeshData()
    mesh.name = fields.get("m_Name", "")
    mesh.compression = fields.get("m_MeshCompression", 0)
    mesh.index_format = fields.get("m_IndexFormat", 0)
    if mesh.compression:
        raise MeshError("mesh %r uses compression %d (unsupported)"
                        % (mesh.name, mesh.compression))

    for submesh in fields.get("m_SubMeshes") or ():
        mesh.submeshes.append((submesh.get("firstByte", 0),
                               submesh.get("indexCount", 0),
                               submesh.get("topology", 0),
                               submesh.get("baseVertex", 0),
                               submesh.get("firstVertex", 0),
                               submesh.get("vertexCount", 0)))

    buffer = fields.get("m_IndexBuffer") or b""
    if not isinstance(buffer, (bytes, bytearray)):
        buffer = bytes(bytearray(buffer))
    if mesh.index_format == 0:
        count = len(buffer) // 2
        mesh.indices = list(struct.unpack("<%dH" % count, buffer[:count * 2]))
    else:
        count = len(buffer) // 4
        mesh.indices = list(struct.unpack("<%dI" % count, buffer[:count * 4]))

    vertex_data = fields.get("m_VertexData") or {}
    mesh.vertex_count = vertex_data.get("m_VertexCount", 0)
    channels = vertex_data.get("m_Channels") or []
    blob = vertex_data.get("m_DataSize") or b""
    _decode_vertex_data(mesh, channels, blob)
    if cache is not None:
        cache[path_id] = mesh
    return mesh


def read_mesh_fields(reader, tree):
    wanted = ("m_Name", "m_MeshCompression", "m_IndexFormat", "m_IndexBuffer",
              "m_VertexData", "m_SubMeshes")
    result = {}
    for child in tree.root.children:
        if child.name in wanted:
            result[child.name] = typetree.read_value(reader, child)
        else:
            typetree.read_value(reader, child)
    return result


def _channel_stride(channels):
    stride = 0
    for channel in channels:
        fmt = channel.get("format", 0)
        dimension = channel.get("dimension", 0)
        if dimension == 0 or fmt not in FORMATS:
            continue
        width = FORMATS[fmt][1]
        end = channel.get("offset", 0) + width * dimension
        stride = max(stride, end)
    return (stride + 3) & ~3


def _decode_vertex_data(mesh, channels, blob):
    count = mesh.vertex_count
    if count == 0 or not blob:
        return
    stride = _channel_stride(channels)
    if stride == 0 or count * stride != len(blob):
        if count and len(blob) % count == 0:
            stride = len(blob) // count
        else:
            raise MeshError("mesh %r vertex blob %d bytes does not tile %d "
                            "vertices (stride %d)"
                            % (mesh.name, len(blob), count, stride))
    # m_Channels is indexed by Unity's VertexAttribute enum: 0 position,
    # 1 normal, 2 tangent, 3 colour, 4 uv0, ...
    for attribute, channel in enumerate(channels):
        offset = channel.get("offset", 0)
        fmt = channel.get("format", 0)
        dimension = channel.get("dimension", 0)
        if dimension == 0 or fmt not in FORMATS:
            continue
        code, width, scale = FORMATS[fmt]
        values = _unpack_channel(blob, count, stride, offset, code, width,
                                 dimension, scale)
        if attribute == ATTRIBUTE_POSITION:
            mesh.positions = _as_tuples(values, 3)
        elif attribute == ATTRIBUTE_NORMAL:
            mesh.normals = _as_tuples(values, 3)
        elif attribute == ATTRIBUTE_TEXCOORD0:
            mesh.uvs = _as_tuples(values, 2)


def _unpack_channel(blob, count, stride, offset, code, width, dimension, scale):
    """Extract one channel out of an interleaved vertex blob."""
    inner = "<%d%s" % (dimension, code)
    size = width * dimension
    view = memoryview(blob)
    if offset == 0 and size == stride:
        raw = view[:count * stride]
        values = list(struct.unpack("<%d%s" % (count * dimension, code), raw))
    else:
        values = []
        unpack = struct.Struct(inner).unpack_from
        append = values.extend
        position = offset
        for _ in range(count):
            append(unpack(view, position))
            position += stride
    if scale is not None:
        values = [v / scale for v in values]
    return values


def _as_tuples(values, size):
    return [tuple(values[i:i + size]) for i in range(0, len(values), size)]


# --------------------------------------------------------------------------
# .nlmesh writer (little endian)
# --------------------------------------------------------------------------

def write_nlmesh(path, positions, normals, uvs, indices, trim=None):
    flags = 0
    vertex_count = len(positions)
    if normals:
        flags |= FLAG_NORMALS
    if uvs:
        flags |= FLAG_UV0
    if trim:
        flags |= FLAG_TRIM
    if indices and max(indices) > 0xFFFF:
        flags |= FLAG_INDEX32

    out = bytearray()
    out += NLHM_MAGIC
    out += struct.pack("<III I", NLHM_VERSION, flags, vertex_count, len(indices))
    out += array.array("f", [c for p in positions for c in p]).tobytes()
    if flags & FLAG_NORMALS:
        out += array.array("f", [c for n in normals for c in n]).tobytes()
    if flags & FLAG_UV0:
        out += array.array("f", [c for uv in uvs for c in uv]).tobytes()
    if flags & FLAG_TRIM:
        out += array.array("f", trim).tobytes()
    if flags & FLAG_INDEX32:
        out += array.array("I", indices).tobytes()
    else:
        out += array.array("H", indices).tobytes()
    with open(path, "wb") as handle:
        handle.write(out)
    return len(out)


def read_nlmesh(path):
    """Read a .nlmesh file back (used by --verify)."""
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != NLHM_MAGIC:
        raise MeshError("not a .nlmesh file: %r" % (path,))
    version, flags, vertex_count, index_count = struct.unpack_from("<IIII", data, 4)
    if version != NLHM_VERSION:
        raise MeshError("unsupported .nlmesh version %d" % version)
    offset = 20
    positions = _read_floats(data, offset, vertex_count * 3)
    offset += vertex_count * 12
    normals = []
    uvs = []
    trim = []
    if flags & FLAG_NORMALS:
        normals = _read_floats(data, offset, vertex_count * 3)
        offset += vertex_count * 12
    if flags & FLAG_UV0:
        uvs = _read_floats(data, offset, vertex_count * 2)
        offset += vertex_count * 8
    if flags & FLAG_TRIM:
        trim = _read_floats(data, offset, vertex_count)
        offset += vertex_count * 4
    if flags & FLAG_INDEX32:
        indices = list(struct.unpack_from("<%dI" % index_count, data, offset))
    else:
        indices = list(struct.unpack_from("<%dH" % index_count, data, offset))
    return {
        "flags": flags,
        "vertexCount": vertex_count,
        "indexCount": index_count,
        "positions": positions,
        "normals": normals,
        "uvs": uvs,
        "trim": trim,
        "indices": indices,
        "bytes": len(data),
    }


def _read_floats(data, offset, count):
    return list(array.array("f", data[offset:offset + count * 4]))
