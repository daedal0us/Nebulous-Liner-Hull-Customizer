"""Unity Addressables support: the aa/catalog.json lookup tables and the
texture bundles they point at.

The JSON catalog keeps its three lookup tables in base64 blobs written by
ContentCatalogData (all integers little endian):

  m_KeyDataString     u32 keyCount, then per key {u8 tag, payload}
                        tag 0 ascii string  -> u32 byteCount + bytes
                        tag 1 unicode       -> u32 charCount + 2 * charCount
                        tag 2 u16, tag 3 u32, tag 4 i32 -> 2 or 4 bytes
                        tag 5 hash128 -> 16 raw bytes, tag 6 type -> i32
  m_BucketDataString  u32 bucketCount, then per bucket
                        {u32 keyOffset, u32 entryCount, u32 entryIndex * n}
  m_EntryDataString   u32 entryCount, then per entry 7 x i32
                        {internalId, provider, dependency, dependencyHash,
                         data, primaryKey, resourceType}

A bucket names its key by the byte offset of the key object inside the key
blob, so keyOffset has to be resolved through the parsed key table.  An
entry's internalId and dependency are indices into m_InternalIds: the first
names the asset, the second names the bundle the asset is streamed from (-1
when the entry *is* a bundle).  Reading a GUID key therefore yields
"(asset path, *.bundle basename)".
"""

from __future__ import annotations

import base64
import json
import os
import struct

from . import serialized, unityfs

CLASS_ASSET_BUNDLE = 142

TAG_ASCII = 0
TAG_UNICODE = 1
TAG_UINT16 = 2
TAG_UINT32 = 3
TAG_INT32 = 4
TAG_HASH128 = 5
TAG_TYPE = 6
TAG_JSON = 7

#: index of the entry fields that point into m_InternalIds
ENTRY_INTERNAL_ID = 0
ENTRY_DEPENDENCY = 2
ENTRY_FIELD_COUNT = 7

GUID_CHARS = frozenset("0123456789abcdef")


class CatalogError(Exception):
    pass


def is_guid(text):
    return (isinstance(text, str) and len(text) == 32
            and set(text) <= GUID_CHARS)


# --------------------------------------------------------------------------
# binary tables
# --------------------------------------------------------------------------

def read_keys(blob):
    """[(byte offset, key object), ...] for m_KeyDataString."""
    if len(blob) < 4:
        raise CatalogError("key data is %d bytes, expected at least 4" % len(blob))
    count = struct.unpack_from("<I", blob, 0)[0]
    keys = []
    pos = 4
    while pos < len(blob):
        start = pos
        tag = blob[pos]
        pos += 1
        if tag in (TAG_ASCII, TAG_JSON):
            length = struct.unpack_from("<I", blob, pos)[0]
            pos += 4
            raw = blob[pos:pos + length]
            pos += length
            keys.append((start, _text(raw, "ascii string at %d" % start)))
        elif tag == TAG_UNICODE:
            length = struct.unpack_from("<I", blob, pos)[0]
            pos += 4
            raw = blob[pos:pos + 2 * length]
            pos += 2 * length
            keys.append((start, _text(raw, "unicode string at %d" % start,
                                      "utf-16-le")))
        elif tag == TAG_UINT16:
            keys.append((start, struct.unpack_from("<H", blob, pos)[0]))
            pos += 2
        elif tag == TAG_UINT32:
            keys.append((start, struct.unpack_from("<I", blob, pos)[0]))
            pos += 4
        elif tag == TAG_INT32:
            keys.append((start, struct.unpack_from("<i", blob, pos)[0]))
            pos += 4
        elif tag == TAG_HASH128:
            keys.append((start, blob[pos:pos + 16].hex()))
            pos += 16
        elif tag == TAG_TYPE:
            keys.append((start, struct.unpack_from("<i", blob, pos)[0]))
            pos += 4
        else:
            raise CatalogError("unknown key tag %d at offset %d" % (tag, start))
    if len(keys) != count:
        raise CatalogError("key table declares %d keys, parsed %d"
                           % (count, len(keys)))
    if pos != len(blob):
        raise CatalogError("key table ends at %d of %d bytes" % (pos, len(blob)))
    return keys


def _text(raw, where, encoding="utf-8"):
    try:
        return raw.decode(encoding)
    except UnicodeDecodeError as exc:
        raise CatalogError("%s is not %s: %s" % (where, encoding, exc))


def read_buckets(blob):
    """[(key offset, (entry index, ...)), ...] for m_BucketDataString."""
    if len(blob) < 4:
        raise CatalogError("bucket data is %d bytes, expected at least 4"
                           % len(blob))
    count = struct.unpack_from("<I", blob, 0)[0]
    buckets = []
    pos = 4
    for _ in range(count):
        key_offset, entry_count = struct.unpack_from("<II", blob, pos)
        pos += 8
        entries = (struct.unpack_from("<%dI" % entry_count, blob, pos)
                   if entry_count else ())
        pos += 4 * entry_count
        buckets.append((key_offset, entries))
    if pos != len(blob):
        raise CatalogError("bucket table ends at %d of %d bytes"
                           % (pos, len(blob)))
    return buckets


def read_entries(blob):
    """[(internalId, provider, dependency, hash, data, key, type), ...]."""
    if len(blob) < 4:
        raise CatalogError("entry data is %d bytes, expected at least 4"
                           % len(blob))
    count = struct.unpack_from("<I", blob, 0)[0]
    stride = 4 * ENTRY_FIELD_COUNT
    expected = 4 + stride * count
    if len(blob) != expected:
        raise CatalogError("entry table is %d bytes, %d entries need %d"
                           % (len(blob), count, expected))
    return [struct.unpack_from("<%di" % ENTRY_FIELD_COUNT, blob,
                               4 + stride * index)
            for index in range(count)]


# --------------------------------------------------------------------------
# catalog
# --------------------------------------------------------------------------

class Catalog(dict):
    """guid -> (asset path, bundle basename), plus folder / bundle helpers."""

    def __init__(self, aa_dir, catalog_path, internal_ids, entries):
        dict.__init__(self)
        self.aa_dir = aa_dir
        self.catalog_path = catalog_path
        self.internal_ids = internal_ids
        self.entries = entries
        self.by_folder = {}
        #: lowercase ship folder name -> addressable ship folder, e.g.
        #: "bulker" -> "Assets/Ship Hulls/F2/Line Ships/Bulker"
        self.folder_roots = {}
        self.bundle_names = set()
        for value in internal_ids:
            if value.lower().endswith(".bundle"):
                self.bundle_names.add(_basename(value))

    def resolve(self, guid):
        """(asset path, bundle basename) for a GUID, or None when unknown."""
        return dict.get(self, guid)

    def asset_path(self, guid):
        found = dict.get(self, guid)
        return found[0] if found else None

    def find_in_folder(self, folder, prefix, suffix):
        """The single GUID of <folder>/<prefix>*<suffix>, None if ambiguous."""
        stems = self.by_folder.get(folder)
        if not stems:
            return None
        matches = [guid for stem, guid in stems.items()
                   if stem.startswith(prefix) and stem.endswith(suffix)]
        return matches[0] if len(matches) == 1 else None

    def bundle_path(self, basename):
        for candidate in (os.path.join(self.aa_dir, "StandaloneWindows64",
                                       basename),
                          os.path.join(self.aa_dir, basename)):
            if os.path.exists(candidate):
                return candidate
        return None

    def __repr__(self):
        return "<Catalog %d assets, %d bundles>" % (len(self),
                                                    len(self.bundle_names))


def bundle_of(index, internal_ids, key_order):
    """Basename of the bundle an entry depends on, or None.

    The dependency index is documented as an m_InternalIds index, but the
    shipped catalog also indexes the parallel key table with it; both tables
    start with the same twelve bundle paths, so the first one that names a
    .bundle wins.
    """
    for table in (internal_ids, key_order):
        if 0 <= index < len(table):
            value = table[index]
            if isinstance(value, str) and value.lower().endswith(".bundle"):
                return _basename(value)
    return None


def _basename(path):
    return path.replace("\\", "/").rsplit("/", 1)[-1]


def find_catalog(aa_dir):
    """Path of the JSON catalog inside an Addressables output directory."""
    plain = os.path.join(aa_dir, "catalog.json")
    if os.path.exists(plain):
        return plain
    candidates = []
    for root in (aa_dir, os.path.join(aa_dir, "StandaloneWindows64")):
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            lowered = name.lower()
            if lowered.startswith("catalog") and lowered.endswith(".json"):
                candidates.append(os.path.join(root, name))
    if not candidates:
        raise CatalogError("no catalog.json under %r" % (aa_dir,))
    return candidates[0]


def load_catalog(aa_dir):
    """Parse <aa_dir>/catalog.json into a Catalog of guid -> asset + bundle."""
    if not os.path.isdir(aa_dir):
        raise CatalogError("Addressables directory %r does not exist"
                           % (aa_dir,))
    path = find_catalog(aa_dir)
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    internal_ids = data.get("m_InternalIds") or []
    if not internal_ids:
        raise CatalogError("%s has no m_InternalIds" % path)
    keys = read_keys(_blob(data, "m_KeyDataString"))
    buckets = read_buckets(_blob(data, "m_BucketDataString"))
    entries = read_entries(_blob(data, "m_EntryDataString"))

    catalog = Catalog(os.path.abspath(aa_dir), path, internal_ids, entries)
    key_at = dict(keys)
    verify_tables(internal_ids, entries, keys, buckets, key_at)

    key_order = [value for _, value in keys]
    for key_offset, entry_indexes in buckets:
        key = key_at.get(key_offset)
        if not is_guid(key):
            continue
        for index in entry_indexes:
            entry = entries[index]
            asset = _internal(internal_ids, entry[ENTRY_INTERNAL_ID])
            if asset is None:
                continue
            catalog[key] = (asset, bundle_of(entry[ENTRY_DEPENDENCY],
                                             internal_ids, key_order))
            folder = os.path.dirname(asset)
            catalog.by_folder.setdefault(folder, {})[
                os.path.splitext(_basename(asset))[0]] = key
            if folder.endswith(" Textures"):
                root = os.path.dirname(folder)
                catalog.folder_roots[_basename(root).lower()] = root
    if not catalog:
        raise CatalogError("%s resolved no GUID assets" % path)
    return catalog


def _blob(data, name):
    encoded = data.get(name)
    if not encoded:
        raise CatalogError("catalog is missing %s" % name)
    return base64.b64decode(encoded)


def _internal(internal_ids, index):
    if 0 <= index < len(internal_ids):
        return internal_ids[index]
    return None


def verify_tables(internal_ids, entries, keys, buckets, key_at):
    """Hard assertions: the blobs have to agree with each other.

    The JSON catalog carries no plain-text copy of these tables, so the checks
    are structural: the three blobs must consume exactly their declared record
    counts, every bucket must name a real key and real entries, every entry
    must index real m_InternalIds, and at least one GUID must resolve to an
    asset path.  A layout drift (wrong field width or field order) breaks the
    blob lengths or the cross references immediately, which is what makes the
    GUID lookup trustworthy.
    """
    if len(buckets) != len(keys):
        raise CatalogError("catalog has %d buckets for %d keys"
                           % (len(buckets), len(keys)))
    for key_offset, entry_indexes in buckets:
        if key_offset not in key_at:
            raise CatalogError("bucket key offset %d is not a key start"
                               % key_offset)
        if not entry_indexes:
            raise CatalogError("bucket at %d lists no entries" % key_offset)
        for index in entry_indexes:
            if not 0 <= index < len(entries):
                raise CatalogError("bucket references entry %d of %d"
                                   % (index, len(entries)))
    for entry in entries:
        if not -1 <= entry[ENTRY_INTERNAL_ID] < len(internal_ids):
            raise CatalogError("entry internal id %d is past m_InternalIds (%d)"
                               % (entry[ENTRY_INTERNAL_ID], len(internal_ids)))
        if not -1 <= entry[ENTRY_DEPENDENCY] < max(len(internal_ids), len(keys)):
            raise CatalogError("entry dependency %d is past both tables"
                               % entry[ENTRY_DEPENDENCY])
    resolved = 0
    for key_offset, entry_indexes in buckets:
        key = key_at[key_offset]
        if not is_guid(key):
            continue
        for index in entry_indexes:
            asset = _internal(internal_ids, entries[index][ENTRY_INTERNAL_ID])
            if asset and asset.startswith("Assets/"):
                resolved += 1
    if not resolved:
        raise CatalogError("no GUID resolves to an Assets/ internal id")
    return resolved


# --------------------------------------------------------------------------
# bundles
# --------------------------------------------------------------------------

class BundleAssets(object):
    """An opened Addressables bundle with its asset path -> pathID table."""

    def __init__(self, path):
        self.path = path
        self.bundle = unityfs.open_bundle(path)
        self.serialized = serialized.open_from_bundle(self.bundle)
        self.resource_stream = unityfs.resource_stream(self.bundle)
        self.container = self._read_container()

    def _read_container(self):
        table = {}
        for info in self.serialized.objects_of(CLASS_ASSET_BUNDLE):
            value = self.serialized.deserialize(info)
            for entry in value.get("m_Container") or ():
                if not isinstance(entry, dict):
                    continue
                name = entry.get("first")
                asset = entry.get("second") or {}
                path_id = (asset.get("asset") or {}).get("m_PathID")
                if name and path_id:
                    table[name] = path_id
        return table

    def path_id(self, asset_path):
        return self.container.get(asset_path)

    def close(self):
        try:
            self.bundle.close()
        except Exception:
            pass

    def __repr__(self):
        return "<BundleAssets %s %d assets>" % (
            _basename(self.path), len(self.container))


class BundlePool(object):
    """Opens catalog bundles on demand; unresolvable ones are skipped."""

    def __init__(self, catalog, log=None):
        self.catalog = catalog
        self.log = log or (lambda message: None)
        self.open = {}
        self.failures = []

    def bundle(self, basename):
        if basename in self.open:
            return self.open[basename]
        path = self.catalog.bundle_path(basename)
        if path is None:
            self._fail(basename, "bundle file is missing from the aa directory")
            return None
        try:
            opened = BundleAssets(path)
        except Exception as exc:
            self._fail(basename, "%s: %s" % (type(exc).__name__, exc))
            return None
        self.open[basename] = opened
        return opened

    def texture(self, guid):
        """(serialized file, pathID, resource stream) for a texture GUID."""
        found = self.catalog.resolve(guid)
        if not found:
            return None
        asset_path, basename = found
        if not basename:
            return None
        opened = self.bundle(basename)
        if opened is None:
            return None
        path_id = opened.path_id(asset_path)
        if not path_id:
            self._fail(basename, "asset %r is not in the bundle container"
                       % asset_path)
            return None
        return opened.serialized, path_id, opened.resource_stream

    def _fail(self, basename, message):
        self.failures.append((basename, message))
        self.log("    aa bundle %s skipped: %s" % (basename, message))

    def close(self):
        for opened in self.open.values():
            opened.close()
        self.open.clear()
