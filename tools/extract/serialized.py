"""SerializedFile (version 22) reader.

Header is 48 bytes big endian, everything after it is little endian::

    u32 metadataSize | u32 fileSize | u32 version | u32 dataOffset
    u8  endianness   | u8[3] reserved
    u32 metadataSize | i64 fileSize | i64 dataOffset | i64 unknown

Measured on stock-f2: metadataSize = 1,343,882, fileSize = 647,688,056,
version = 22, dataOffset = 1,344,960.  The header's dataOffset is *not* where
object payloads start (it is rounded up to 1 KiB); payloads begin at
align16(48 + metadataSize).  Object byteStart values are relative to that.

Metadata layout (all little endian)::

    cstr unityVersion | i32 targetPlatform | u8 enableTypeTree
    i32 typeCount            + typeCount * type entry
    i32 objectCount | align 4 | objectCount * 24 bytes
    i32 scriptTypeCount      + scriptTypeCount * 12 bytes  (v >= 11)
    i32 externalCount        + externalCount * (cstr, 16 B guid, i32 type, cstr)
    i32 refTypeCount         + refTypeCount * type entry   (v >= 20)
    cstr userInformation                                   (v >= 5)
"""

from __future__ import annotations

import struct

from . import typetree

HEADER_SIZE = 48

CLASS_NAMES = {
    1: "GameObject", 4: "Transform", 21: "Material", 23: "MeshRenderer",
    28: "Texture2D", 33: "MeshFilter", 43: "Mesh", 48: "Shader",
    49: "TextAsset", 74: "AnimationClip", 114: "MonoBehaviour",
    115: "MonoScript", 128: "Font", 142: "AssetBundle", 224: "RectTransform",
}


def align_up(value, alignment=16):
    remainder = value % alignment
    return value if remainder == 0 else value + alignment - remainder


class SerializedFileError(Exception):
    pass


class ObjectInfo(object):
    __slots__ = ("path_id", "byte_start", "byte_size", "type_id", "class_id")

    def __init__(self, path_id, byte_start, byte_size, type_id):
        self.path_id = path_id
        self.byte_start = byte_start
        self.byte_size = byte_size
        self.type_id = type_id
        self.class_id = -1

    def __repr__(self):
        return "Object(%d, +%d/%d, type=%d, class=%d)" % (
            self.path_id, self.byte_start, self.byte_size, self.type_id,
            self.class_id)


class SerializedType(object):
    __slots__ = ("class_id", "is_stripped", "script_type_index", "script_id",
                 "old_type_hash", "tree")

    def __init__(self):
        self.class_id = 0
        self.is_stripped = 0
        self.script_type_index = -1
        self.script_id = None
        self.old_type_hash = None
        self.tree = None

    def __repr__(self):
        return "SerializedType(class=%d, script=%d, nodes=%s)" % (
            self.class_id, self.script_type_index,
            len(self.tree.nodes) if self.tree is not None else None)


class External(object):
    __slots__ = ("path", "guid", "type")

    def __init__(self, path, guid, type_):
        self.path = path
        self.guid = guid
        self.type = type_

    def __repr__(self):
        return "External(%r)" % (self.path,)


class ScriptType(object):
    __slots__ = ("flag", "hash")

    def __init__(self, flag, hash_):
        self.flag = flag
        self.hash = hash_

    def __repr__(self):
        return "ScriptType(flag=%d, hash=%s)" % (self.flag, self.hash.hex())


class SerializedFile(object):
    """Parses one CAB node; object payloads are read on demand."""

    def __init__(self, source, name=""):
        self.source = source
        self.name = name
        head = source.read_at(0, HEADER_SIZE)
        if len(head) != HEADER_SIZE:
            raise SerializedFileError("short serialized file header")
        (self.legacy_metadata_size, self.legacy_file_size, self.version,
         self.legacy_data_offset) = struct.unpack_from(">IIII", head, 0)
        self.endianness = head[16]
        (self.metadata_size, self.file_size, self.declared_data_offset,
         self.unknown) = struct.unpack_from(">Iqqq", head, 20)
        if self.endianness != 0:
            raise SerializedFileError("big endian serialized file is unsupported")
        if self.version != 22:
            raise SerializedFileError("expected SerializedFile version 22, got %d"
                                      % self.version)
        self._parse_metadata()
        self._index_objects()

    # -- metadata ----------------------------------------------------------
    def _parse_metadata(self):
        meta = self.source.read_at(HEADER_SIZE, self.metadata_size)
        if len(meta) != self.metadata_size:
            raise SerializedFileError("truncated metadata")
        reader = typetree.Reader(meta, 0, self.metadata_size)
        self.unity_version = reader.cstring()
        self.target_platform = reader.i32()
        self.enable_type_tree = reader.u8() != 0

        self.types = [self._read_type(reader) for _ in range(reader.i32())]

        object_count = reader.i32()
        reader.align(4)
        self.objects = []
        for _ in range(object_count):
            path_id = reader.i64()
            byte_start = reader.i64()
            byte_size = reader.u32()
            type_id = reader.i32()
            if not 0 <= type_id < len(self.types):
                raise SerializedFileError("object %d references type id %d of %d"
                                          % (path_id, type_id, len(self.types)))
            info = ObjectInfo(path_id, byte_start, byte_size, type_id)
            info.class_id = self.types[type_id].class_id
            self.objects.append(info)

        self.script_types = []
        if self.version >= 11:
            for _ in range(reader.i32()):
                self.script_types.append(ScriptType(reader.u32(), reader.u64()))

        self.externals = []
        for _ in range(reader.i32()):
            if self.version >= 6:
                reader.cstring()
            guid = reader.read(16)
            type_ = reader.i32()
            self.externals.append(External(reader.cstring(), guid, type_))

        self.ref_types = []
        if self.version >= 20:
            for _ in range(reader.i32()):
                self.ref_types.append(self._read_type(reader, is_ref_type=True))

        self.user_information = reader.cstring() if self.version >= 5 else ""

        if reader.pos != self.metadata_size:
            raise SerializedFileError(
                "metadata parse ended at %d, header says %d (%d bytes left)"
                % (reader.pos, self.metadata_size,
                   self.metadata_size - reader.pos))

    def _read_type(self, reader, is_ref_type=False):
        entry = SerializedType()
        entry.class_id = reader.i32()
        entry.is_stripped = reader.u8()
        entry.script_type_index = reader.i16()
        if entry.class_id == 114 or entry.script_type_index >= 0:
            entry.script_id = reader.read(16)
        if self.version >= 17 or is_ref_type:
            entry.old_type_hash = reader.read(16)
        if self.enable_type_tree:
            node_count = reader.i32()
            buffer_size = reader.i32()
            nodes = []
            for _ in range(node_count):
                (node_version, level, type_flags, type_offset, name_offset,
                 byte_size, index, meta_flag) = struct.unpack_from(
                    "<HBBIIiiI", reader.data, reader.pos)
                reader.pos += 24
                ref_type_hash = 0
                if self.version >= 19:
                    ref_type_hash = struct.unpack_from("<Q", reader.data,
                                                       reader.pos)[0]
                    reader.pos += 8
                nodes.append(typetree.TypeNode(
                    node_version, level, type_flags, type_offset, name_offset,
                    byte_size, index, meta_flag, ref_type_hash))
            string_buffer = reader.read(buffer_size)
            entry.tree = typetree.TypeTree(nodes, string_buffer)
            for _ in range(reader.i32()):
                reader.i32()
        return entry

    # -- objects -----------------------------------------------------------
    def _index_objects(self):
        self.object_base = align_up(HEADER_SIZE + self.metadata_size, 16)
        self.by_path = {}
        self.by_class = {}
        for info in self.objects:
            self.by_path[info.path_id] = info
            self.by_class.setdefault(info.class_id, []).append(info)
        if self.objects:
            self._check_payloads()

    def _check_payloads(self):
        lowest = min(o.byte_start for o in self.objects)
        highest = max(o.byte_start + o.byte_size for o in self.objects)
        if lowest != 0:
            raise SerializedFileError(
                "object payloads do not start at the data area (min offset %d)"
                % lowest)
        if self.object_base + highest != self.file_size:
            raise SerializedFileError(
                "object payloads end at %d, file size %d (off by %d)"
                % (self.object_base + highest, self.file_size,
                   self.file_size - self.object_base - highest))

    def object_data(self, info):
        return self.source.read_at(self.object_base + info.byte_start,
                                   info.byte_size)

    def reader(self, info):
        return typetree.Reader(self.object_data(info), 0, info.byte_size)

    def deserialize(self, info):
        entry = self.types[info.type_id]
        if entry.tree is None or not entry.tree.nodes:
            return {}
        return typetree.read_type_tree(self.reader(info), entry.tree)

    def objects_of(self, class_id):
        return list(self.by_class.get(class_id, ()))

    def class_histogram(self):
        counts = {}
        for info in self.objects:
            counts[info.class_id] = counts.get(info.class_id, 0) + 1
        return counts

    def class_name(self, class_id):
        return CLASS_NAMES.get(class_id, "Class%d" % class_id)

    # -- validation --------------------------------------------------------
    def canary(self, limit=None, min_size=0, max_size=None):
        """Deserialize objects and check the type tree consumes byteSize."""
        checked = 0
        failures = []
        for info in self.objects:
            if max_size is not None and info.byte_size > max_size:
                continue
            if info.byte_size < min_size:
                continue
            entry = self.types[info.type_id]
            if entry.tree is None or not entry.tree.nodes:
                continue
            if limit is not None and checked >= limit:
                break
            checked += 1
            try:
                reader = self.reader(info)
                typetree.read_type_tree(reader, entry.tree)
            except Exception as exc:
                failures.append((info, "raised %s: %s" % (type(exc).__name__, exc)))
                continue
            if reader.pos != info.byte_size:
                failures.append((info, "consumed %d of %d bytes"
                                 % (reader.pos, info.byte_size)))
        return checked, failures

    # -- convenience -------------------------------------------------------
    def mono_scripts(self):
        """pathID -> MonoScript class name."""
        names = {}
        for info in self.by_class.get(115, ()):
            try:
                value = self.deserialize(info)
            except Exception:
                continue
            names[info.path_id] = value.get("m_ClassName") or ""
        return names

    def component_script_path_ids(self):
        """pathID -> MonoScript pathID for every MonoBehaviour instance."""
        result = {}
        for info in self.by_class.get(114, ()):
            if self.types[info.type_id].tree is None:
                continue
            try:
                value = self.deserialize(info)
            except Exception:
                continue
            pptr = value.get("m_Script")
            if isinstance(pptr, dict) and pptr.get("m_PathID"):
                result[info.path_id] = pptr["m_PathID"]
        return result

    def text_assets(self):
        return [self.deserialize(info) for info in self.by_class.get(49, ())]

    def __repr__(self):
        return ("<SerializedFile %s v%d unity=%s types=%d objects=%d>"
                % (self.name or "?", self.version, self.unity_version,
                   len(self.types), len(self.objects)))


def open_from_bundle(bundle, node_index=0):
    from .unityfs import NodeStream
    node = bundle.nodes[node_index]
    return SerializedFile(NodeStream(bundle, node), node.path)
