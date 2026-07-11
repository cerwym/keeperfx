#!/usr/bin/env python3
"""Bootstrap transcoder: legacy indexed .dat/.tab (+ .pal) -> .fxspr v2.

This is the derisking bootstrap for the truecolour sprite pipeline
(docs/sprite-asset-unification.md). It lets the engine-side loader / RGBA atlas
be exercised against real .fxspr files. It expands each legacy RLE sprite to
straight RGBA8 via a palette lookup, exactly mirroring the engine:

  - RLE decode:  bflib_sprite.h::LbSpriteDecode
                 (0 = end row, cmd<0 = skip -cmd transparent, cmd>0 = copy cmd
                 index bytes; opaque source index 0 is remapped to 1).
  - palette:     6-bit VGA components scaled to 8-bit via <<2
                 (SpriteMaterialise.cpp), index 0 => transparent (alpha 0).

Because it is fed already-quantised legacy art, the RGBA it emits is palette
colour, not true 24-bit source colour. That is fine for bootstrapping: the
container / loader / atlas / renderer wiring are identical regardless of where
the RGBA came from.

Output is the self-describing v2 container: rich 32-byte directory entries, an
optional string table (names) and asset-info block, and a zlib(deflate)-
compressed payload. Byte layout is defined by src/kfx/assets/FxSprFormat.h.
Keep the two in sync.
"""
import argparse
import json
import struct
import sys
import zlib

TAB_ENTRY = struct.Struct("<IBB")   # offset:uint32, width:uint8, height:uint8

# ── .fxspr v2 struct layouts (mirror FxSprFormat.h) ──────────────────────────
FXSPR_HEADER = struct.Struct("<4sHHHHIII")   # magic,ver,flags,kind,reserved,count,dir_off,pay_off
FXSPR_EXT2 = struct.Struct("<HHIIIII")        # entry_stride,assetinfo_size,assetinfo_off,
                                              # stringtable_off,stringtable_size,payload_size,payload_raw_size
FXSPR_ENTRY_RICH = struct.Struct("<IHHhhhHIHBBIHH")  # 32 bytes (see FxSprEntryRich)
FXSPR_ASSETINFO = struct.Struct("<HBBII")     # scale,colour_mode,provenance,name_off,reserved

FXSPR_VERSION = 2
FXSPR_KIND_SPRITESHEET = 1

FXSPR_FLAG_DIMS16 = 0x0002
FXSPR_FLAG_PAYLOAD_COMPRESSED = 0x0004
FXSPR_FLAG_RICH_ENTRIES = 0x0010

FXSPR_VIEW_TOPDOWN = 0

# provenance / colour-mode enums
PROV = {"unknown": 0, "bullfrog": 1, "keeperfx": 2, "mod": 3}
COLOUR = {"indexed": 0, "truecolour": 1}

# category name -> FxSprCategory enum
CATEGORY = {
    "unknown": 0, "gui": 1, "font": 2, "pointer": 3, "creature": 4,
    "object": 5, "room": 6, "texture": 7, "effect": 8,
}


def read_tab(path):
    data = open(path, "rb").read()
    n = len(data) // TAB_ENTRY.size
    entries = []
    for i in range(n):
        off, w, h = TAB_ENTRY.unpack_from(data, i * TAB_ENTRY.size)
        entries.append((off, w, h))
    return entries


def sprite_sizes(entries, dat_len):
    """Mirror spritesheet.cpp: size of each sprite = gap to the next sorted offset."""
    order = sorted(range(len(entries)), key=lambda i: entries[i][0])
    sorted_offs = [entries[i][0] for i in order] + [dat_len]
    size_by_index = [0] * len(entries)
    for rank, idx in enumerate(order):
        size_by_index[idx] = sorted_offs[rank + 1] - sorted_offs[rank]
    return size_by_index


def decode_rle(dat, start, size, w, h):
    """Return a w*h bytearray of palette indices (0 = transparent). Matches
    LbSpriteDecode: rows zero-filled, cmd stream, index 0 -> 1 when opaque."""
    out = bytearray(w * h)
    p = start
    end = start + size
    for y in range(h):
        row = y * w
        x = 0
        while True:
            if p >= end:
                break
            cmd = dat[p]
            p += 1
            if cmd >= 128:      # signed byte < 0  => skip transparent
                x += 256 - cmd
                continue
            if cmd == 0:        # end of row
                break
            for _ in range(cmd):  # copy `cmd` literal index bytes
                if p >= end:
                    break
                idx = dat[p]
                p += 1
                if x < w:
                    row_i = row + x
                    out[row_i] = idx if idx else 1
                x += 1
    return out


def load_palette(path):
    pal = open(path, "rb").read()
    if len(pal) < 768:
        raise SystemExit(f"palette {path} too small ({len(pal)} bytes, need 768)")
    return pal


def indices_to_rgba(idx_px, w, h, pal):
    out = bytearray(w * h * 4)
    for i, idx in enumerate(idx_px):
        o = i * 4
        if idx == 0:
            continue  # transparent (already zeroed)
        out[o + 0] = (pal[idx * 3 + 0] << 2) & 0xFF
        out[o + 1] = (pal[idx * 3 + 1] << 2) & 0xFF
        out[o + 2] = (pal[idx * 3 + 2] << 2) & 0xFF
        out[o + 3] = 255
    return out


class StringTable:
    """Builds a NUL-terminated string blob; byte 0 is the empty string."""

    def __init__(self):
        self._blob = bytearray(b"\x00")
        self._offsets = {"": 0}

    def intern(self, s):
        if not s:
            return 0
        if s in self._offsets:
            return self._offsets[s]
        off = len(self._blob)
        self._blob += s.encode("utf-8") + b"\x00"
        self._offsets[s] = off
        return off

    def bytes(self):
        return bytes(self._blob)


def transcode(tab_path, dat_path, pal_path, out_path,
              scale=0, provenance="bullfrog", colour_mode="indexed",
              category="unknown", names=None, display_name=None, compress=True):
    """Transcode one indexed sheet to a v2 .fxspr.

    names: optional dict {entry_index: name} for per-sprite names.
    display_name: optional file-level collection name.
    """
    entries = read_tab(tab_path)
    dat = open(dat_path, "rb").read()
    pal = load_palette(pal_path)
    sizes = sprite_sizes(entries, len(dat))

    names = names or {}
    cat_id = CATEGORY.get(str(category).lower(), 0)

    strtab = StringTable()
    display_name_off = strtab.intern(display_name or "")

    # Build the (uncompressed) payload and the rich directory in tandem.
    directory = bytearray()
    payload = bytearray()
    for i, (off, w, h) in enumerate(entries):
        data_off = len(payload)
        if w > 0 and h > 0:
            idx_px = decode_rle(dat, off, sizes[i], w, h)
            payload += indices_to_rgba(idx_px, w, h, pal)
        name_off = strtab.intern(names.get(i, names.get(str(i), "")))
        directory += FXSPR_ENTRY_RICH.pack(
            data_off, w, h,
            0, 0, 0, 0,          # offset_x, offset_y, shadow_offset, frame_flags
            i,                    # group_id = index (flat sheet identity)
            0, 0, FXSPR_VIEW_TOPDOWN,  # frame_index, rotation, view
            name_off, cat_id, 0)  # name_off, category, reserved

    raw_payload = bytes(payload)
    payload_raw_size = len(raw_payload)
    on_disk_payload = zlib.compress(raw_payload, 9) if compress else raw_payload
    payload_size = len(on_disk_payload)

    strtab_blob = strtab.bytes()

    # Layout: header, ext2, assetinfo, stringtable, directory, payload.
    header_size = FXSPR_HEADER.size          # 24
    ext2_off = header_size                    # 24
    assetinfo_off = ext2_off + FXSPR_EXT2.size  # 48
    stringtable_off = assetinfo_off + FXSPR_ASSETINFO.size  # 60
    stringtable_size = len(strtab_blob)
    dir_off = stringtable_off + stringtable_size
    pay_off = dir_off + len(directory)

    flags = FXSPR_FLAG_DIMS16 | FXSPR_FLAG_RICH_ENTRIES
    if compress:
        flags |= FXSPR_FLAG_PAYLOAD_COMPRESSED

    header = FXSPR_HEADER.pack(
        b"FXSP", FXSPR_VERSION, flags, FXSPR_KIND_SPRITESHEET, 0,
        len(entries), dir_off, pay_off)
    ext2 = FXSPR_EXT2.pack(
        FXSPR_ENTRY_RICH.size, FXSPR_ASSETINFO.size, assetinfo_off,
        stringtable_off, stringtable_size, payload_size, payload_raw_size)
    assetinfo = FXSPR_ASSETINFO.pack(
        int(scale), COLOUR.get(str(colour_mode).lower(), 0),
        PROV.get(str(provenance).lower(), 0), display_name_off, 0)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(ext2)
        f.write(assetinfo)
        f.write(strtab_blob)
        f.write(directory)
        f.write(on_disk_payload)
    return len(entries), payload_size


def _parse_v2(blob):
    """Return (count, dir_off, entries[list of rich tuples], inflated_payload, info)."""
    magic, ver, flags, kind, _res, count, dir_off, pay_off = FXSPR_HEADER.unpack_from(blob, 0)
    assert magic == b"FXSP", magic
    assert ver == FXSPR_VERSION, ver
    (stride, ai_size, ai_off, st_off, st_size,
     payload_size, payload_raw_size) = FXSPR_EXT2.unpack_from(blob, FXSPR_HEADER.size)
    assert stride == FXSPR_ENTRY_RICH.size, stride
    comp = blob[pay_off:pay_off + payload_size]
    if flags & FXSPR_FLAG_PAYLOAD_COMPRESSED:
        payload = zlib.decompress(comp)
    else:
        payload = comp
    assert len(payload) == payload_raw_size, (len(payload), payload_raw_size)
    entries = [FXSPR_ENTRY_RICH.unpack_from(blob, dir_off + i * stride) for i in range(count)]
    info = FXSPR_ASSETINFO.unpack_from(blob, ai_off) if ai_off else None
    return count, entries, payload, (flags, kind, info)


def verify(out_path, tab_path):
    blob = open(out_path, "rb").read()
    count, entries, payload, (flags, kind, info) = _parse_v2(blob)
    tab = read_tab(tab_path)
    assert count == len(tab), (count, len(tab))
    opaque = 0
    for i in range(count):
        (data_off, w, h, ox, oy, shd, ff, gid, fidx, rot, view,
         name_off, cat, res) = entries[i]
        assert (w, h) == (tab[i][1], tab[i][2]), (i, w, h, tab[i])
        assert gid == i, (i, gid)  # flat-sheet identity
        base = data_off
        span = w * h * 4
        assert base + span <= len(payload), (i, base, span, len(payload))
        for px in range(base + 3, base + span, 4):
            if payload[px] != 0:
                opaque += 1
    assert (flags & FXSPR_FLAG_RICH_ENTRIES) != 0
    print(f"[verify] OK  v2 entries={count} raw_payload={len(payload)}B "
          f"file={len(blob)}B compressed={(flags & FXSPR_FLAG_PAYLOAD_COMPRESSED) != 0} "
          f"opaque_px={opaque}")


def dump_png(out_path, index, png_path):
    blob = open(out_path, "rb").read()
    _count, entries, payload, _ = _parse_v2(blob)
    data_off, w, h = entries[index][0], entries[index][1], entries[index][2]
    base = data_off
    raw = b""
    for y in range(h):
        raw += b"\x00" + payload[base + y * w * 4: base + (y + 1) * w * 4]

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(png_path, "wb").write(png)
    print(f"[dump] sprite #{index} {w}x{h} -> {png_path}")


def main():
    ap = argparse.ArgumentParser(description="legacy .dat/.tab (+.pal) -> .fxspr v2")
    ap.add_argument("--tab", required=True)
    ap.add_argument("--dat", required=True)
    ap.add_argument("--pal", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scale", type=int, default=0, help="nominal scale tier (e.g. 32/64)")
    ap.add_argument("--provenance", default="bullfrog", choices=list(PROV))
    ap.add_argument("--colour-mode", default="indexed", choices=list(COLOUR))
    ap.add_argument("--category", default="unknown", choices=list(CATEGORY))
    ap.add_argument("--display-name", default=None)
    ap.add_argument("--names", default=None,
                    help="optional JSON file mapping entry index -> name")
    ap.add_argument("--no-compress", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--dump-png", metavar="IDX", type=int, default=None)
    ap.add_argument("--dump-out", default="sprite.png")
    args = ap.parse_args()

    names = None
    if args.names:
        names = json.load(open(args.names, "r", encoding="utf-8"))

    n, pay = transcode(args.tab, args.dat, args.pal, args.out,
                       scale=args.scale, provenance=args.provenance,
                       colour_mode=args.colour_mode, category=args.category,
                       names=names, display_name=args.display_name,
                       compress=not args.no_compress)
    print(f"[fxspr] wrote {args.out}: {n} entries, {pay} payload bytes (on disk)")
    if args.verify:
        verify(args.out, args.tab)
    if args.dump_png is not None:
        dump_png(args.out, args.dump_png, args.dump_out)


if __name__ == "__main__":
    sys.exit(main())
