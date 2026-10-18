"""Texture2D decoding (BC1 / BC3 / BC7 / RGBA32 / RGB24 / BGRA32 / ARGB32 /
Alpha8), channel statistics, box downsampling and 8 bit PNG output.

Hull materials in stock-f2 only use BC1 (format 10) and BC3 (format 12); the
other decoders exist so the pipeline degrades gracefully instead of failing.
Image data is usually streamed into the bundle's .resS node (m_StreamData), so
callers pass that node stream in.
"""

from __future__ import annotations

import struct
import zlib

from . import hierarchy

TEXTURE_FORMAT_NAMES = {
    1: "Alpha8", 3: "RGB24", 4: "RGBA32", 5: "ARGB32", 7: "RGB565",
    10: "BC1", 12: "BC3", 14: "BGRA32", 25: "BC7", 62: "ASTC_4x4",
}

FORMAT_ALPHA8 = 1
FORMAT_RGB24 = 3
FORMAT_RGBA32 = 4
FORMAT_ARGB32 = 5
FORMAT_RGB565 = 7
FORMAT_BC1 = 10
FORMAT_BC3 = 12
FORMAT_BGRA32 = 14
FORMAT_BC7 = 25

SUPPORTED = (FORMAT_ALPHA8, FORMAT_RGB24, FORMAT_RGBA32, FORMAT_ARGB32,
             FORMAT_RGB565, FORMAT_BC1, FORMAT_BC3, FORMAT_BGRA32, FORMAT_BC7)


class TextureError(Exception):
    pass


class TextureInfo(object):
    __slots__ = ("name", "width", "height", "format", "mip_count",
                 "image_data", "stream_offset", "stream_size", "stream_path")

    def __init__(self, value):
        self.name = value.get("m_Name", "")
        self.width = value.get("m_Width", 0)
        self.height = value.get("m_Height", 0)
        self.format = value.get("m_TextureFormat", 0)
        self.mip_count = max(1, value.get("m_MipCount", 1))
        data = value.get("m_ImageData") or b""
        if not isinstance(data, (bytes, bytearray)):
            data = bytes(bytearray(data))
        self.image_data = data
        stream = value.get("m_StreamData") or {}
        self.stream_offset = stream.get("offset", 0)
        self.stream_size = stream.get("size", 0)
        self.stream_path = stream.get("path", "")

    @property
    def format_name(self):
        return TEXTURE_FORMAT_NAMES.get(self.format, "format%d" % self.format)

    def payload(self, resource_stream=None):
        """Raw bytes of the texture (all mips, from mip 0)."""
        if self.stream_size:
            if resource_stream is None:
                raise TextureError("texture %r is streamed but no .resS node was "
                                   "provided" % (self.name,))
            data = resource_stream.read_at(self.stream_offset, self.stream_size)
            if len(data) != self.stream_size:
                raise TextureError("short read for streamed texture %r"
                                   % (self.name,))
            return data
        return self.image_data

    def __repr__(self):
        return "TextureInfo(%r %dx%d %s mips=%d stream=%d)" % (
            self.name, self.width, self.height, self.format_name,
            self.mip_count, self.stream_size)


def read_texture(serialized_file, path_id):
    info = serialized_file.by_path.get(path_id)
    if info is None:
        raise TextureError("texture %d not found" % path_id)
    return TextureInfo(serialized_file.deserialize(info))


def mip_size(fmt, width, height):
    if fmt in (FORMAT_BC1, FORMAT_BC7):
        return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 8
    if fmt == FORMAT_BC3:
        return max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * 16
    return _pixel_size(fmt) * width * height


def _pixel_size(fmt):
    return {FORMAT_ALPHA8: 1, FORMAT_RGB24: 3, FORMAT_RGBA32: 4,
            FORMAT_ARGB32: 4, FORMAT_BGRA32: 4, FORMAT_RGB565: 2}.get(fmt, 4)


def pick_mip(texture, max_size):
    """Largest mip whose width is <= max_size (never upscales)."""
    width, height = texture.width, texture.height
    level = 0
    offset = 0
    best = (0, 0, width, height)
    while level < texture.mip_count and width >= 1 and height >= 1:
        if width <= max_size:
            return level, offset, width, height
        best = (level, offset, width, height)
        offset += mip_size(texture.format, width, height)
        width = max(1, width // 2)
        height = max(1, height // 2)
        level += 1
    return best


def mip_offset(texture, level):
    """(byte offset, width, height) of one mip level in the payload."""
    width, height = texture.width, texture.height
    offset = 0
    for _ in range(level):
        offset += mip_size(texture.format, width, height)
        width = max(1, width // 2)
        height = max(1, height // 2)
    return offset, width, height


def decode_mip(texture, level, resource_stream=None):
    """Decode one mip level; returns (width, height, r, g, b, a bytearrays).

    Only the bytes of that level are read: a streamed 4096 px map has a 22 MiB
    payload but its 512 px level is 128 KiB, and the extractor never needs more.
    """
    offset, width, height = mip_offset(texture, level)
    size = mip_size(texture.format, width, height)
    if texture.stream_size:
        if resource_stream is None:
            raise TextureError("texture %r is streamed but no .resS node was "
                               "provided" % (texture.name,))
        if offset == 0 and size == texture.stream_size:
            data = texture.payload(resource_stream)
        else:
            data = resource_stream.read_at(texture.stream_offset + offset, size)
        if len(data) != size:
            raise TextureError("short read for streamed texture %r mip %d"
                               % (texture.name, level))
    else:
        data = texture.image_data
        if offset + size > len(data):
            raise TextureError("texture %r mip %d runs past its %d payload bytes"
                               % (texture.name, level, len(data)))
        data = data[offset:offset + size]
    return decode_image(texture.format, width, height, data)


def decode_image(fmt, width, height, data):
    if fmt in (FORMAT_RGBA32, FORMAT_ARGB32, FORMAT_BGRA32):
        return _decode_32(width, height, data, fmt)
    if fmt == FORMAT_RGB24:
        return _decode_rgb24(width, height, data)
    if fmt == FORMAT_ALPHA8:
        return _decode_alpha8(width, height, data)
    if fmt == FORMAT_BC1:
        return _decode_bc1(width, height, data)
    if fmt == FORMAT_BC3:
        return _decode_bc3(width, height, data)
    if fmt == FORMAT_BC7:
        return _decode_bc7(width, height, data)
    raise TextureError("unsupported texture format %d (%s)"
                       % (fmt, TEXTURE_FORMAT_NAMES.get(fmt, "?")))


def _channels(length):
    return (bytearray(length), bytearray(length), bytearray(length),
            bytearray(length))


def _decode_32(width, height, data, fmt):
    count = width * height
    red, green, blue, alpha = _channels(count)
    order = {FORMAT_RGBA32: (0, 1, 2, 3), FORMAT_ARGB32: (1, 2, 3, 0),
             FORMAT_BGRA32: (2, 1, 0, 3)}[fmt]
    r_index, g_index, b_index, a_index = order
    for i in range(count):
        base = i * 4
        red[i] = data[base + r_index]
        green[i] = data[base + g_index]
        blue[i] = data[base + b_index]
        alpha[i] = data[base + a_index]
    return width, height, red, green, blue, alpha


def _decode_rgb24(width, height, data):
    count = width * height
    red, green, blue, alpha = _channels(count)
    for i in range(count):
        base = i * 3
        red[i] = data[base]
        green[i] = data[base + 1]
        blue[i] = data[base + 2]
        alpha[i] = 255
    return width, height, red, green, blue, alpha


def _decode_alpha8(width, height, data):
    count = width * height
    red, green, blue, alpha = _channels(count)
    for i in range(count):
        value = data[i]
        red[i] = green[i] = blue[i] = value
        alpha[i] = 255
    return width, height, red, green, blue, alpha


def _rgb565(value):
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def _decode_bc1(width, height, data):
    count = width * height
    red, green, blue, alpha = _channels(count)
    blocks_x = (width + 3) // 4
    blocks_y = (height + 3) // 4
    pos = 0
    for by in range(blocks_y):
        for bx in range(blocks_x):
            c0, c1, bits = struct.unpack_from("<HH", data, pos)[0], \
                struct.unpack_from("<HH", data, pos + 2)[0], \
                struct.unpack_from("<I", data, pos + 4)[0]
            pos += 8
            palette = _bc1_palette(c0, c1)
            for row in range(4):
                y = by * 4 + row
                if y >= height:
                    break
                base = y * width + bx * 4
                shift = row * 8
                for column in range(4):
                    x = bx * 4 + column
                    if x >= width:
                        break
                    index = (bits >> (shift + column * 2)) & 3
                    r, g, b, a = palette[index]
                    pixel = base + column
                    red[pixel] = r
                    green[pixel] = g
                    blue[pixel] = b
                    alpha[pixel] = a
    return width, height, red, green, blue, alpha


def _bc1_palette(c0, c1):
    r0, g0, b0 = (c0 >> 11) & 0x1F, (c0 >> 5) & 0x3F, c0 & 0x1F
    r1, g1, b1 = (c1 >> 11) & 0x1F, (c1 >> 5) & 0x3F, c1 & 0x1F
    r0, g0, b0 = (r0 << 3) | (r0 >> 2), (g0 << 2) | (g0 >> 4), (b0 << 3) | (b0 >> 2)
    r1, g1, b1 = (r1 << 3) | (r1 >> 2), (g1 << 2) | (g1 >> 4), (b1 << 3) | (b1 >> 2)
    if c0 > c1:
        r2 = (2 * r0 + r1) // 3
        g2 = (2 * g0 + g1) // 3
        b2 = (2 * b0 + b1) // 3
        r3 = (r0 + 2 * r1) // 3
        g3 = (g0 + 2 * g1) // 3
        b3 = (b0 + 2 * b1) // 3
        return ((r0, g0, b0, 255), (r1, g1, b1, 255), (r2, g2, b2, 255),
                (r3, g3, b3, 255))
    r2 = (r0 + r1) // 2
    g2 = (g0 + g1) // 2
    b2 = (b0 + b1) // 2
    return ((r0, g0, b0, 255), (r1, g1, b1, 255), (r2, g2, b2, 255),
            (0, 0, 0, 0))


def _decode_bc3(width, height, data):
    count = width * height
    red, green, blue, alpha = _channels(count)
    blocks_x = (width + 3) // 4
    blocks_y = (height + 3) // 4
    pos = 0
    for by in range(blocks_y):
        for bx in range(blocks_x):
            a0 = data[pos]
            a1 = data[pos + 1]
            a_bits = int.from_bytes(data[pos + 2:pos + 8], "little")
            alphas = _bc3_alpha_palette(a0, a1)
            c0 = struct.unpack_from("<H", data, pos + 8)[0]
            c1 = struct.unpack_from("<H", data, pos + 10)[0]
            bits = struct.unpack_from("<I", data, pos + 12)[0]
            pos += 16
            palette = _bc1_palette(c0, c1)
            for row in range(4):
                y = by * 4 + row
                if y >= height:
                    break
                base = y * width + bx * 4
                shift = row * 8
                for column in range(4):
                    x = bx * 4 + column
                    if x >= width:
                        break
                    index = (bits >> (shift + column * 2)) & 3
                    r, g, b, _ = palette[index]
                    pixel = base + column
                    red[pixel] = r
                    green[pixel] = g
                    blue[pixel] = b
                    alpha[pixel] = alphas[(a_bits >> (shift + column * 3)) & 7]
    return width, height, red, green, blue, alpha


def _bc3_alpha_palette(a0, a1):
    values = [a0, a1]
    if a0 > a1:
        for i in range(1, 7):
            values.append(((7 - i) * a0 + i * a1) // 7)
    else:
        for i in range(1, 5):
            values.append(((5 - i) * a0 + i * a1) // 5)
        values.extend((0, 255))
    return values


# --------------------------------------------------------------------------
# channel statistics / downsampling / PNG
# --------------------------------------------------------------------------

def channel_stats(red, green, blue, alpha):
    count = len(red)
    stats = {}
    for name, channel in (("r", red), ("g", green), ("b", blue), ("a", alpha)):
        if not channel:
            continue
        total = sum(channel)
        mean = total / (255.0 * count)
        variance = sum((value * value) for value in channel) / count - (total / count) ** 2
        stats[name] = (mean, (variance ** 0.5) / 255.0)
    return stats


def choose_mask_channel(stats):
    """Channel with the most signal (largest byte deviation wins)."""
    best, best_value = "r", -1.0
    for name in ("r", "g", "b", "a"):
        if name in stats and stats[name][1] > best_value:
            best, best_value = name, stats[name][1]
    return best


def downsample(channel, width, height, target):
    """Box filter an 8 bit channel to at most target x target."""
    factor = max(1, (min(width, height) + target - 1) // target)
    if factor == 1:
        return channel, width, height
    new_width = max(1, width // factor)
    new_height = max(1, height // factor)
    out = bytearray(new_width * new_height)
    for y in range(new_height):
        row = y * factor
        for x in range(new_width):
            column = x * factor
            total = 0
            for dy in range(factor):
                base = (row + dy) * width + column
                for dx in range(factor):
                    total += channel[base + dx]
            out[y * new_width + x] = total // (factor * factor)
    return out, new_width, new_height


def is_grayscale(red, green, blue, tolerance=6):
    """True when the texture is (almost) a luminance image."""
    step = max(1, len(red) // 4096)
    samples = 0
    colourful = 0
    for i in range(0, len(red), step):
        r, g, b = red[i], green[i], blue[i]
        if max(r, g, b) - min(r, g, b) > tolerance:
            colourful += 1
        samples += 1
    return samples > 0 and colourful <= samples * 0.02


def write_png(path, width, height, channels, data):
    """Write an 8 bit PNG; channels is 1 (gray) or 3 (rgb)."""
    color_type = 0 if channels == 1 else 2
    raw = bytearray()
    stride = width * channels
    for y in range(height):
        raw.append(0)
        raw += data[y * stride:(y + 1) * stride]
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)

    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))

    out = bytearray(b"\x89PNG\r\n\x1a\n")
    out += chunk(b"IHDR", header)
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(out)
    return len(out)


def _paeth(left, up, upleft):
    estimate = left + up - upleft
    to_left = abs(estimate - left)
    to_up = abs(estimate - up)
    to_upleft = abs(estimate - upleft)
    if to_left <= to_up and to_left <= to_upleft:
        return left
    return up if to_up <= to_upleft else upleft


def read_png(path):
    """(width, height, channels, pixels) of an 8 bit grey/RGB PNG."""
    with open(path, "rb") as handle:
        blob = handle.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise TextureError("not a PNG: %r" % (path,))
    width = height = channels = 0
    compressed = bytearray()
    pos = 8
    while pos + 8 <= len(blob):
        length = struct.unpack_from(">I", blob, pos)[0]
        kind = blob[pos + 4:pos + 8]
        payload = blob[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color_type = struct.unpack_from(">IIBB", payload, 0)
            if depth != 8 or color_type not in (0, 2):
                raise TextureError("%r is %d bit, colour type %d (only 8 bit "
                                   "grey/RGB are supported)" % (path, depth,
                                                                color_type))
            channels = 1 if color_type == 0 else 3
        elif kind == b"IDAT":
            compressed += payload
        elif kind == b"IEND":
            break
    if not width or not height or not channels:
        raise TextureError("%r has no usable IHDR" % (path,))
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    pixels = bytearray(stride * height)
    previous = bytearray(stride)
    pos = 0
    for row in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if filter_type == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                upleft = previous[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(left, previous[i], upleft)) & 0xFF
        elif filter_type != 0:
            raise TextureError("%r uses PNG filter %d" % (path, filter_type))
        pixels[row * stride:(row + 1) * stride] = line
        previous = line
    return width, height, channels, pixels


def read_png_size(path):
    with open(path, "rb") as handle:
        head = handle.read(33)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        raise TextureError("not a PNG: %r" % (path,))
    width, height, depth, color_type = struct.unpack_from(">IIBB", head, 16)
    return width, height, depth, color_type
