#!/usr/bin/env python3
"""Bootstrap transcoder: legacy indexed .dat/.tab (+ .pal) -> .fxspr v1.

This is the derisking bootstrap for the truecolour sprite pipeline
(docs/sprite-asset-unification.md). It lets the engine-side loader / RGBA atlas
be built and tested against real .fxspr files *before* the C++ pngpal2raw
'-f fxspr' branch exists. It expands each legacy RLE sprite to straight RGBA8
via a palette lookup, exactly mirroring the engine:

  - RLE decode:  bflib_sprite.h::LbSpriteDecode
                 (0 = end row, cmd<0 = skip -cmd transparent, cmd>0 = copy cmd
                 index bytes; opaque source index 0 is remapped to 1).
  - palette:     6-bit VGA components scaled to 8-bit via <<2
                 (SpriteMaterialise.cpp), index 0 => transparent (alpha 0).

Because it is fed already-quantised legacy art, the RGBA it emits is palette
colour, not true 24-bit source colour. That is fine for bootstrapping: the
container / loader / atlas / renderer wiring are identical regardless of where
the RGBA came from. The real pngpal2raw branch will later emit the same
container straight from 24-bit source PNGs (skipping quantisation).

Byte layout is defined by src/kfx/assets/FxSprFormat.h. Keep the two in sync.
"""
import argparse
import struct
import sys
import zlib

TAB_ENTRY = struct.Struct("<IBB")   # offset:uint32, width:uint8, height:uint8
FXSPR_HEADER = struct.Struct("<4sHHHHIII")  # magic,ver,flags,kind,reserved,count,dir_off,pay_off
FXSPR_ENTRY = struct.Struct("<IHH")  # data_off:uint32, width:uint16, height:uint16

FXSPR_VERSION = 1
FXSPR_KIND_SPRITESHEET = 1
FXSPR_FLAG_DIMS16 = 0x0002


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


def transcode(tab_path, dat_path, pal_path, out_path):
    entries = read_tab(tab_path)
    dat = open(dat_path, "rb").read()
    pal = load_palette(pal_path)
    sizes = sprite_sizes(entries, len(dat))

    directory = bytearray()
    payload = bytearray()
    for i, (off, w, h) in enumerate(entries):
        data_off = len(payload)
        if w > 0 and h > 0:
            idx_px = decode_rle(dat, off, sizes[i], w, h)
            payload += indices_to_rgba(idx_px, w, h, pal)
        directory += FXSPR_ENTRY.pack(data_off, w, h)

    dir_off = FXSPR_HEADER.size
    pay_off = dir_off + len(directory)
    header = FXSPR_HEADER.pack(
        b"FXSP", FXSPR_VERSION, FXSPR_FLAG_DIMS16, FXSPR_KIND_SPRITESHEET, 0,
        len(entries), dir_off, pay_off)

    with open(out_path, "wb") as f:
        f.write(header)
        f.write(directory)
        f.write(payload)
    return len(entries), len(payload)


def verify(out_path, tab_path):
    blob = open(out_path, "rb").read()
    magic, ver, flags, kind, reserved, count, dir_off, pay_off = \
        FXSPR_HEADER.unpack_from(blob, 0)
    assert magic == b"FXSP", magic
    assert ver == FXSPR_VERSION, ver
    tab = read_tab(tab_path)
    assert count == len(tab), (count, len(tab))
    # dims must match tab exactly (parity contract)
    opaque = 0
    for i in range(count):
        data_off, w, h = FXSPR_ENTRY.unpack_from(blob, dir_off + i * FXSPR_ENTRY.size)
        assert (w, h) == (tab[i][1], tab[i][2]), (i, w, h, tab[i])
        base = pay_off + data_off
        span = w * h * 4
        assert base + span <= len(blob), (i, base, span, len(blob))
        for px in range(base + 3, base + span, 4):
            if blob[px] != 0:
                opaque += 1
    print(f"[verify] OK  entries={count} dir_off={dir_off} pay_off={pay_off} "
          f"file={len(blob)}B opaque_px={opaque}")


def dump_png(out_path, index, png_path):
    blob = open(out_path, "rb").read()
    _, _, _, _, _, count, dir_off, pay_off = FXSPR_HEADER.unpack_from(blob, 0)
    data_off, w, h = FXSPR_ENTRY.unpack_from(blob, dir_off + index * FXSPR_ENTRY.size)
    base = pay_off + data_off
    raw = b""
    for y in range(h):
        raw += b"\x00" + blob[base + y * w * 4: base + (y + 1) * w * 4]

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
    ap = argparse.ArgumentParser(description="legacy .dat/.tab (+.pal) -> .fxspr v1")
    ap.add_argument("--tab", required=True)
    ap.add_argument("--dat", required=True)
    ap.add_argument("--pal", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--dump-png", metavar="IDX", type=int, default=None)
    ap.add_argument("--dump-out", default="sprite.png")
    args = ap.parse_args()

    n, pay = transcode(args.tab, args.dat, args.pal, args.out)
    print(f"[fxspr] wrote {args.out}: {n} entries, {pay} payload bytes")
    if args.verify:
        verify(args.out, args.tab)
    if args.dump_png is not None:
        dump_png(args.out, args.dump_png, args.dump_out)


if __name__ == "__main__":
    sys.exit(main())
