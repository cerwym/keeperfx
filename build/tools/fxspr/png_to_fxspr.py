#!/usr/bin/env python3
"""Pack FXGraphics source PNGs into a truecolour .fxspr v2 via a crop manifest.

This is the RGBA/truecolour arm of the sprite pipeline: it takes the *source*
art from the FXGraphics repo (https://github.com/dkfans/FXGraphics) and the
same `filelist_*.txt` crop manifests the shipped .dat build uses, but keeps the
straight 24-bit+alpha pixels instead of quantising through a palette. That
proves the higher-quality tiers (sprites-128, textures-128, pointer-256, ...)
end-to-end and gives the comparison viewer genuine truecolour / higher-res
variants to sit beside the palette-derived ones.

Filelist format (mirrors pngpal2raw's imagelist):
  line 0:  "<sheet-name> 0 0 0 0"      -> entry 0, the null sentinel (0x0)
  line N:  "<png>"                     -> whole image is one sprite
           "<png> <x> <y> <w> <h>"     -> crop a sub-rectangle as one sprite
PNG paths are relative to --image-root (default: the filelist's directory).
Point --image-root at a higher-resolution sibling folder (e.g. pointer-256)
while using a lower tier's filelist to upscale a whole sheet.

Entry order + count match the filelist, so entry N is the same logical sprite
as entry N in the indexed .fxspr for the same collection -> the VariantCatalogue
lines them up as quality variants.

Emits v2 truecolour (FxSprColour_Truecolour) via dat_to_fxspr.write_v2.
"""
import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dat_to_fxspr as fx  # noqa: E402


def parse_filelist(path):
    """Return (sheet_name, [(png_rel, x, y, w, h) | (png_rel, None...)]) records.

    The first line is the sentinel (returned as the sheet name); each following
    line is a sprite: either a bare PNG (whole image) or PNG + x y w h crop.
    """
    rows = []
    sheet_name = ""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for lineno, raw in enumerate(f):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if lineno == 0 or (not rows and not sheet_name):
                # first meaningful line = sentinel "<name> 0 0 0 0"
                sheet_name = parts[0]
                rows.append(None)  # placeholder for entry 0 sentinel
                continue
            png = parts[0]
            if len(parts) >= 5:
                try:
                    x, y, w, h = (int(parts[1]), int(parts[2]),
                                  int(parts[3]), int(parts[4]))
                    rows.append((png, x, y, w, h))
                    continue
                except ValueError:
                    pass
            rows.append((png, None, None, None, None))
    return sheet_name, rows


def load_image(cache, root, rel):
    key = os.path.normcase(rel)
    if key in cache:
        return cache[key]
    full = os.path.join(root, rel)
    img = None
    if os.path.isfile(full):
        img = Image.open(full).convert("RGBA")
    cache[key] = img
    return img


def build_sprites(rows, image_root):
    cache = {}
    sprites = []
    missing = 0
    for rec in rows:
        if rec is None:
            sprites.append({"w": 0, "h": 0, "rgba": b""})  # sentinel
            continue
        png, x, y, w, h = rec
        img = load_image(cache, image_root, png)
        if img is None:
            missing += 1
            sprites.append({"w": 0, "h": 0, "rgba": b"", "name": png})
            continue
        if x is None:
            crop = img
        else:
            crop = img.crop((x, y, x + w, y + h))
        cw, ch = crop.size
        sprites.append({"w": cw, "h": ch, "rgba": crop.tobytes(),
                        "name": os.path.splitext(os.path.basename(png))[0]})
    return sprites, missing


def convert(filelist, out_path, image_root=None, scale=0,
            provenance="keeperfx", category="unknown", display_name=None,
            compress=True):
    image_root = image_root or os.path.dirname(os.path.abspath(filelist))
    sheet_name, rows = parse_filelist(filelist)
    sprites, missing = build_sprites(rows, image_root)
    n, payload = fx.write_v2(
        out_path, sprites, scale=scale, provenance=provenance,
        colour_mode="truecolour", category=category,
        display_name=display_name or sheet_name, compress=compress)
    return n, payload, missing


def verify(out_path, expected_count):
    blob = open(out_path, "rb").read()
    count, entries, payload, (flags, _kind, _info) = fx._parse_v2(blob)
    assert count == expected_count, (count, expected_count)
    opaque = 0
    for (data_off, w, h, *_rest) in entries:
        span = w * h * 4
        assert data_off + span <= len(payload), (data_off, span, len(payload))
        for px in range(data_off + 3, data_off + span, 4):
            if payload[px] != 0:
                opaque += 1
    print(f"[verify] OK  truecolour entries={count} raw_payload={len(payload)}B "
          f"file={len(blob)}B opaque_px={opaque}")


def main():
    ap = argparse.ArgumentParser(description="FXGraphics PNGs -> truecolour .fxspr v2")
    ap.add_argument("--filelist", required=True, help="crop manifest (filelist_*.txt)")
    ap.add_argument("--image-root", default=None,
                    help="dir the PNG paths are relative to (default: filelist's dir; "
                         "set to a higher-res sibling folder to upscale a sheet)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--scale", type=int, default=0)
    ap.add_argument("--provenance", default="keeperfx", choices=list(fx.PROV))
    ap.add_argument("--category", default="unknown", choices=list(fx.CATEGORY))
    ap.add_argument("--display-name", default=None)
    ap.add_argument("--no-compress", action="store_true")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    n, payload, missing = convert(
        args.filelist, args.out, image_root=args.image_root, scale=args.scale,
        provenance=args.provenance, category=args.category,
        display_name=args.display_name, compress=not args.no_compress)
    warn = f"  ({missing} missing PNG -> empty)" if missing else ""
    print(f"[fxspr] wrote {args.out}: {n} entries, {payload} payload bytes{warn}")
    if args.verify:
        verify(args.out, n)


if __name__ == "__main__":
    main()
