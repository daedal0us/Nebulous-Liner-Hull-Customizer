"""Carves DXBC shader blobs out of a Unity player data file and disassembles them.

The AssetBundles ship shaders with their code stripped (every sub program keeps only a
blob index), but the built player's data files carry the compiled DXBC containers. This
tool finds those containers by their header, reports which shader resources each one
declares, and can hand a container to the system d3dcompiler so the assembly can be read.
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import struct
import sys

MAGIC = b"DXBC"


def _valid(data, index):
    """Size and chunk offsets of a DXBC container at index, or None."""
    if index + 40 > len(data):
        return None
    version = struct.unpack_from("<I", data, index + 20)[0]
    size = struct.unpack_from("<I", data, index + 24)[0]
    count = struct.unpack_from("<I", data, index + 28)[0]
    if version != 1 or not 1 <= count <= 16 or not 40 <= size <= len(data) - index:
        return None
    offsets = [struct.unpack_from("<I", data, index + 32 + 4 * i)[0] for i in range(count)]
    if offsets[0] != 32 + 4 * count or offsets != sorted(offsets):
        return None
    return size, offsets


def carve(data):
    """[(offset, blob)] for every DXBC container in data."""
    out = []
    cursor = 0
    while True:
        index = data.find(MAGIC, cursor)
        if index < 0:
            return out
        info = _valid(data, index)
        if info is None:
            cursor = index + 4
            continue
        size, _offsets = info
        out.append((index, bytes(data[index:index + size])))
        cursor = index + size


def chunks(blob):
    """{"RDEF": bytes, ...} for one container."""
    count = struct.unpack_from("<I", blob, 28)[0]
    result = {}
    for i in range(count):
        offset = struct.unpack_from("<I", blob, 32 + 4 * i)[0]
        name = blob[offset:offset + 4].decode("latin-1")
        size = struct.unpack_from("<I", blob, offset + 4)[0]
        result[name] = blob[offset + 8:offset + 8 + size]
    return result


def _cstring(blob, offset):
    if offset < 0 or offset >= len(blob):
        return ""
    end = blob.find(b"\0", offset)
    if end < 0:
        return ""
    text = blob[offset:end]
    if not text or len(text) > 120:
        return ""
    try:
        value = text.decode("ascii")
    except UnicodeDecodeError:
        return ""
    return value if value.isprintable() else ""


def rdef_names(body):
    """Resource and constant buffer names declared by an RDEF chunk."""
    if len(body) < 32:
        return [], []
    count_cb, offset_cb, count_res, offset_res = struct.unpack_from("<IIII", body, 0)
    resources = []
    for i in range(min(count_res, 512)):
        base = offset_res + i * 32
        if base + 32 > len(body):
            break
        name_offset = struct.unpack_from("<I", body, base)[0]
        name = _cstring(body, name_offset)
        if name:
            resources.append(name)
    buffers = []
    for i in range(min(count_cb, 64)):
        base = offset_cb + i * 24
        if base + 24 > len(body):
            break
        name_offset = struct.unpack_from("<I", body, base)[0]
        variables, variable_offset = struct.unpack_from("<II", body, base + 4)
        name = _cstring(body, name_offset)
        if name:
            buffers.append(name)
        for v in range(min(variables, 256)):
            vbase = variable_offset + v * 40
            if vbase + 40 > len(body):
                break
            vname = _cstring(body, struct.unpack_from("<I", body, vbase)[0])
            if vname:
                buffers.append(vname)
    return resources, buffers


def strings(blob):
    """Readable ASCII runs in a blob, for a quick look at what it declares."""
    out = []
    run = bytearray()
    for byte in blob:
        if 32 <= byte < 127:
            run.append(byte)
            continue
        if len(run) >= 4:
            out.append(run.decode("ascii"))
        run = bytearray()
    if len(run) >= 4:
        out.append(run.decode("ascii"))
    return out


def _compiler():
    for name in (os.path.join(os.getcwd(), "dist", "bin", "d3dcompiler_47.dll"),
                 "d3dcompiler_47.dll"):
        try:
            return ctypes.WinDLL(name)
        except OSError:
            continue
    return None


def disassemble(blob):
    """D3D assembly text for one container, or "" when the system cannot do it."""
    library = _compiler()
    if library is None:
        return ""
    library.D3DDisassemble.restype = ctypes.c_long
    buffer = ctypes.create_string_buffer(blob, len(blob))
    output = ctypes.c_void_p()
    hr = library.D3DDisassemble(ctypes.byref(buffer), ctypes.c_size_t(len(blob)),
                                ctypes.c_uint(0), None, ctypes.byref(output))
    if hr != 0 or not output:
        return ""
    # ID3DBlob: vtable pointer, then GetBufferPointer / GetBufferSize
    vtable = ctypes.cast(output, ctypes.POINTER(ctypes.c_void_p))[0]
    entries = ctypes.cast(vtable, ctypes.POINTER(ctypes.c_void_p))
    this = ctypes.c_void_p
    get_buffer = ctypes.WINFUNCTYPE(ctypes.c_void_p, this)(entries[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, this)(entries[4])
    release = ctypes.WINFUNCTYPE(ctypes.c_ulong, this)(entries[2])
    pointer = get_buffer(output)
    size = get_size(output)
    text = ctypes.string_at(pointer, size).decode("latin-1")
    release(output)
    return text


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv:
        print(__doc__)
        return 2
    root = argv[0]
    match = ""
    out_dir = ""
    limit = 0
    survey = False
    i = 1
    while i < len(argv):
        if argv[i] == "--survey":
            survey = True
            i += 1
        elif argv[i] == "--match":
            match = argv[i + 1]
            i += 2
        elif argv[i] == "--out":
            out_dir = argv[i + 1]
            i += 2
        elif argv[i] == "--limit":
            limit = int(argv[i + 1])
            i += 2
        else:
            i += 1
    files = sorted(f for f in os.listdir(root) if f.endswith((".assets", ".resource")))
    files = [os.path.join(root, f) for f in files] + [
        os.path.join(root, f) for f in sorted(os.listdir(root))
        if f.startswith(("level", "globalgamemanagers")) and not f.endswith(".resS")]
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    written = 0
    histogram = {}
    ranked = []
    for path in files:
        try:
            data = open(path, "rb").read()
        except OSError:
            continue
        found = carve(data)
        if not found:
            continue
        if not survey:
            print("%-26s containers %d" % (os.path.basename(path), len(found)))
        for offset, blob in found:
            if survey:
                text = disassemble(blob)
                kind = ""
                for token in ("ps_5_0", "vs_5_0", "cs_5_0", "gs_5_0", "hs_5_0", "ds_5_0"):
                    if token in text:
                        kind = token
                        break
                histogram[kind or "-"] = histogram.get(kind or "-", 0) + 1
                samples = text.count("sample")
                if kind.startswith(("ps_", "vs_")):
                    ranked.append((samples, kind, os.path.basename(path), offset, len(blob)))
                continue
            names = strings(blob)
            hit = match and any(match in name for name in names)
            if match and not hit:
                continue
            parts = chunks(blob)
            resources, buffers = ([], [])
            if "RDEF" in parts:
                resources, buffers = rdef_names(parts["RDEF"])
            line = "   @%-9d size=%-6d chunks=%-28s res=%s" % (
                offset, len(blob), ",".join(sorted(parts)),
                ",".join(resources[:4]) if resources else "-")
            print(line)
            if hit and out_dir:
                written += 1
                stem = os.path.join(out_dir, "%s_%d" % (os.path.basename(path).split(".")[0],
                                                        offset))
                with open(stem + ".bin", "wb") as handle:
                    handle.write(blob)
                text = disassemble(blob)
                if text:
                    with open(stem + ".asm", "w", encoding="latin-1", newline="\n") as handle:
                        handle.write(text)
                if limit and written >= limit:
                    return 0
    if survey:
        print("program types:", ", ".join("%s=%d" % item for item in sorted(histogram.items())))
        ranked.sort(reverse=True)
        for samples, kind, name, offset, size in ranked[:10]:
            print("   %-22s @%-9d %s samples=%-3d size=%d" % (name, offset, kind, samples, size))
        if not ranked:
            print("   no vertex or fragment program in this data")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
