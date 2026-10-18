"""Reader for UnityFS asset bundles (format version 8 / Unity 2020.3).

Pure standard library.  Nothing is ever loaded whole: the bundle is opened and
compressed blocks are fetched on demand, then cached in a small LRU.

Layout::

    sig UnityFS nul | u32be version | cstr unityVersion | cstr unityRevision
    | i64be size | u32be compressedBlocksInfoSize
    | u32be uncompressedBlocksInfoSize | u32be flags
    -> align 16 -> blocksInfo (LZ4 block compressed)
    -> align 16 -> data blocks, no padding between them

blocksInfo (big endian)::

    hash[16] | u32 blockCount | blockCount * (u32 uncompressedSize,
                                            u32 compressedSize, u16 flags)
    | u32 nodeCount | nodeCount * (i64 offset, i64 size, u32 flags, cstr path)
"""

from __future__ import annotations

import bisect
import io
import os
import struct

SIGNATURE = b"UnityFS" + bytes([0])

FLAG_BLOCKS_AND_DIRECTORY_COMBINED = 0x40
FLAG_BLOCK_INFO_AT_END = 0x80
FLAG_PADDED = 0x200

COMPRESSION_NONE = 0
COMPRESSION_LZMA = 1
COMPRESSION_LZ4 = 2
COMPRESSION_LZ4HC = 3
COMPRESSION_LZHAM = 4

COMPRESSION_NAMES = {
    COMPRESSION_NONE: "none",
    COMPRESSION_LZMA: "lzma",
    COMPRESSION_LZ4: "lz4",
    COMPRESSION_LZ4HC: "lz4hc",
    COMPRESSION_LZHAM: "lzham",
}

_ALIGN = 16


class UnityFSError(Exception):
    """Raised when a bundle does not match the expected UnityFS layout."""


def align_up(value, alignment=_ALIGN):
    remainder = value % alignment
    return value if remainder == 0 else value + alignment - remainder

# --------------------------------------------------------------------------
# LZ4 block format (raw, no frame header) -- self implemented on purpose.
# --------------------------------------------------------------------------

def lz4_decompress(src, dest_size=-1):
    """Decode one raw LZ4 block.

    Sequence layout: token byte (4 bit literal length | 4 bit match length-4),
    literal bytes, u16le match offset, optional match length continuation.
    A 15 in either nibble means "read 255-terminated continuation bytes".
    """
    n = len(src)
    i = 0
    out = bytearray()
    while i < n:
        token = src[i]
        i += 1
        lit_len = token >> 4
        if lit_len == 15:
            while True:
                b = src[i]
                i += 1
                lit_len += b
                if b != 255:
                    break
        if lit_len:
            end = i + lit_len
            if end > n:
                raise UnityFSError("lz4: literal run past end of input")
            out += src[i:end]
            i = end
        if i >= n:
            break
        if i + 2 > n:
            raise UnityFSError("lz4: truncated match offset")
        offset = src[i] | (src[i + 1] << 8)
        i += 2
        if offset == 0:
            raise UnityFSError("lz4: zero match offset")
        match_len = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                b = src[i]
                i += 1
                match_len += b
                if b != 255:
                    break
        start = len(out) - offset
        if start < 0:
            raise UnityFSError("lz4: match offset before start of output")
        if offset >= match_len:
            out += out[start:start + match_len]
        else:
            unit = bytes(out[start:start + offset])
            reps, rem = divmod(match_len, offset)
            out += unit * reps
            if rem:
                out += unit[:rem]
    if dest_size >= 0 and len(out) != dest_size:
        raise UnityFSError(
            "lz4: decoded %d bytes, header claims %d" % (len(out), dest_size))
    return bytes(out)


def decompress(kind, data, dest_size):
    if kind == COMPRESSION_NONE:
        return data
    if kind in (COMPRESSION_LZ4, COMPRESSION_LZ4HC):
        return lz4_decompress(data, dest_size)
    raise UnityFSError("unsupported compression %r (%s)"
                       % (kind, COMPRESSION_NAMES.get(kind, "?")))

class Block(object):
    __slots__ = ("uncompressed_size", "compressed_size", "flags",
                 "file_offset", "stream_offset")

    def __init__(self, uncompressed_size, compressed_size, flags):
        self.uncompressed_size = uncompressed_size
        self.compressed_size = compressed_size
        self.flags = flags
        self.file_offset = 0     # offset of the compressed bytes in the bundle
        self.stream_offset = 0   # offset of the decoded bytes in node space

    @property
    def compression(self):
        return self.flags & 0x3F

    def __repr__(self):
        return ("Block(comp=%d, uncomp=%d, %s)"
                % (self.compressed_size, self.uncompressed_size,
                   COMPRESSION_NAMES.get(self.compression, self.compression)))


class Node(object):
    __slots__ = ("offset", "size", "flags", "path")

    def __init__(self, offset, size, flags, path):
        self.offset = offset
        self.size = size
        self.flags = flags
        self.path = path

    def __repr__(self):
        return "Node(%r, offset=%d, size=%d)" % (self.path, self.offset, self.size)


def _read_cstring(buf, pos):
    end = buf.index(0 if isinstance(buf, (bytes, bytearray)) else "\x00", pos)
    raw = buf[pos:end]
    if isinstance(raw, (bytes, bytearray)):
        return raw.decode("utf-8", "replace"), end + 1
    return raw, end + 1


class Bundle(object):
    """A lazily decoded UnityFS bundle."""

    def __init__(self, path, cache_blocks=96):
        self.path = path
        self.file_size = os.path.getsize(path)
        self._fh = open(path, "rb")
        try:
            self._parse_header()
            self._parse_blocks_info()
        except Exception:
            self._fh.close()
            raise
        self._cache = {}
        self._cache_order = []
        self._cache_blocks = cache_blocks

    # -- construction ------------------------------------------------------
    def _parse_header(self):
        fh = self._fh
        fh.seek(0)
        raw = fh.read(12)
        if raw[:8] != SIGNATURE:
            raise UnityFSError("not a UnityFS bundle: %r" % (raw[:8],))
        self.version = struct.unpack(">I", raw[8:12])[0]
        tail = fh.read(512)
        pos = 0
        self.unity_version, pos = _read_cstring(tail, pos)
        self.unity_revision, pos = _read_cstring(tail, pos)
        header_end = 12 + pos
        fh.seek(header_end)
        fixed = fh.read(20)
        if len(fixed) != 20:
            raise UnityFSError("truncated UnityFS header")
        (self.size, self.compressed_blocks_info_size,
         self.uncompressed_blocks_info_size, self.flags) = struct.unpack(
            ">qIII", fixed)
        self.header_end = header_end + 20
        self.compression = self.flags & 0x3F

    def _parse_blocks_info(self):
        start = align_up(self.header_end)
        if self.flags & FLAG_BLOCK_INFO_AT_END:
            start = self.file_size - self.compressed_blocks_info_size
        self.blocks_info_offset = start
        self._fh.seek(start)
        compressed = self._fh.read(self.compressed_blocks_info_size)
        if len(compressed) != self.compressed_blocks_info_size:
            raise UnityFSError("truncated blocksInfo")
        data = decompress(self.compression, compressed,
                          self.uncompressed_blocks_info_size)
        self.blocks_info = data
        self.blocks_info_hash = data[:16]

        pos = 16
        block_count = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        blocks = []
        for _ in range(block_count):
            u, c, f = struct.unpack_from(">IIH", data, pos)
            pos += 10
            blocks.append(Block(u, c, f))
        node_count = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        nodes = []
        for _ in range(node_count):
            off, size, flags = struct.unpack_from(">qqI", data, pos)
            pos += 20
            path, pos = _read_cstring(data, pos)
            nodes.append(Node(off, size, flags, path))
        self.blocks = blocks
        self.nodes = nodes

        self.data_start = align_up(start + self.compressed_blocks_info_size)
        cursor = self.data_start
        stream = 0
        for block in blocks:
            block.file_offset = cursor
            block.stream_offset = stream
            cursor += block.compressed_size
            stream += block.uncompressed_size
        self.stream_size = stream
        self._block_stream_starts = [b.stream_offset for b in blocks]

    # -- integrity ---------------------------------------------------------
    def check_layout(self):
        total_compressed = sum(b.compressed_size for b in self.blocks)
        expected = self.data_start + total_compressed
        ok = expected == self.file_size
        detail = ("data_start(%d) + sum(compressedSize)(%d) = %d, fileSize = %d"
                  % (self.data_start, total_compressed, expected,
                     self.file_size))
        return ok, detail

    @property
    def block_count(self):
        return len(self.blocks)

    @property
    def total_uncompressed(self):
        return self.stream_size

    def node(self, path):
        for node in self.nodes:
            if node.path == path:
                return node
        raise UnityFSError("no node %r in bundle (have %r)"
                           % (path, [n.path for n in self.nodes]))

    # -- block level random access ----------------------------------------
    def _read_block(self, index):
        cached = self._cache.get(index)
        if cached is not None:
            return cached
        block = self.blocks[index]
        self._fh.seek(block.file_offset)
        raw = self._fh.read(block.compressed_size)
        if len(raw) != block.compressed_size:
            raise UnityFSError("truncated block %d" % index)
        data = decompress(block.compression, raw, block.uncompressed_size)
        self._cache[index] = data
        order = self._cache_order
        order.append(index)
        if len(order) > self._cache_blocks:
            self._cache.pop(order.pop(0), None)
        return data

    def read_stream(self, offset, size):
        """Read size decoded bytes starting at a node-space stream offset."""
        if offset < 0 or size < 0 or offset + size > self.stream_size:
            raise UnityFSError(
                "stream read out of range: offset=%d size=%d stream=%d"
                % (offset, size, self.stream_size))
        if size == 0:
            return b""
        starts = self._block_stream_starts
        index = bisect.bisect_right(starts, offset) - 1
        if index < 0:
            index = 0
        chunks = []
        remaining = size
        cursor = offset
        while remaining > 0:
            block = self.blocks[index]
            local = cursor - block.stream_offset
            take = min(remaining, block.uncompressed_size - local)
            payload = self._read_block(index)
            chunks.append(payload[local:local + take])
            cursor += take
            remaining -= take
            index += 1
        return b"".join(chunks)

    def close(self):
        if self._fh is not None:
            self._fh.close()
            self._fh = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __repr__(self):
        return ("<Bundle %r unity=%s blocks=%d nodes=%d uncompressed=%d>"
                % (os.path.basename(self.path), self.unity_version,
                   len(self.blocks), len(self.nodes), self.stream_size))


class NodeStream(io.RawIOBase):
    """Seekable read-only view of one node inside a bundle."""

    def __init__(self, bundle, node):
        self._bundle = bundle
        self.node = node
        self._pos = 0
        self.name = node.path
        self.length = node.size

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self._pos

    def seek(self, offset, whence=io.SEEK_SET):
        if whence == io.SEEK_SET:
            new = offset
        elif whence == io.SEEK_CUR:
            new = self._pos + offset
        elif whence == io.SEEK_END:
            new = self.node.size + offset
        else:
            raise ValueError("bad whence %r" % (whence,))
        if new < 0:
            raise UnityFSError("negative seek")
        self._pos = new
        return new

    def read(self, size=-1):
        if size is None or size < 0:
            size = self.node.size - self._pos
        size = min(size, self.node.size - self._pos)
        if size <= 0:
            return b""
        data = self._bundle.read_stream(self.node.offset + self._pos, size)
        self._pos += len(data)
        return data

    def read_at(self, offset, size):
        if offset + size > self.node.size:
            raise UnityFSError("read_at past node end")
        return self._bundle.read_stream(self.node.offset + offset, size)

    def __len__(self):
        return self.node.size


def open_bundle(path):
    bundle = Bundle(path)
    ok, detail = bundle.check_layout()
    if not ok:
        bundle.close()
        raise UnityFSError("bundle layout self check failed: " + detail)
    return bundle
