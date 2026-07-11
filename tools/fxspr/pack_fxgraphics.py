#!/usr/bin/env python3
"""Batch-pack the FXGraphics higher-quality source art into truecolour .fxspr v2.

The shipped build only rasterises the -32/-64 palette tiers of each sheet. The
FXGraphics repo also carries higher-quality source PNGs (truecolour, and for
some sheets 128px/higher-res AI-upscaled art) that never get packed. This driver
runs those through png_to_fxspr so the comparison viewer gets genuine
truecolour / higher-res variants sitting beside the palette-derived ones, proving
a further level of asset quality end-to-end.

Two kinds of variant are produced, both written as `<base>-<scale>-tc.fxspr`
(the `-tc` marker keeps make_manifest grouping them with their indexed siblings;
the colour mode itself is read back out of the file header):

  * TRUECOLOUR at an EXISTING scale -- a straight RGBA re-pack of a sheet we
    already ship indexed (gui1, font1). Entry count must match the indexed
    .fxspr exactly (parity_ref), so entry N lines up as the same logical sprite.

  * HIGHER-RES truecolour -- textures at 128px paired with a 32px truecolour
    baseline. Auto-discovered for every texture sheet present in BOTH
    textures-32 and textures-128 with matching entry counts.

Nothing here is committed as data: outputs land in .deploy/data/fxspr (git-
ignored, derived). Commit the tools + the regenerated manifest instead, then
re-run make_manifest.py.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import png_to_fxspr as p2f  # noqa: E402


# Truecolour re-packs of sheets we already ship indexed. Each entry:
#   (base, scale, filelist_rel, image_root_rel_or_None, category, parity_ref)
# filelist/image_root are relative to --fx-root; parity_ref is a .fxspr in the
# output dir whose entry count MUST match (guarantees per-entry alignment).
GUI_FONT_VARIANTS = [
    ("gui1", 32, r"menufx\gui1-32\filelist_gui1.txt", None, "gui", "gui1-32.fxspr"),
    ("gui1", 64, r"menufx\gui1-64\filelist_gui1.txt", None, "gui", "gui1-64.fxspr"),
    ("font1", 32, r"enginefx\font_simp-32\filelist_font1.txt", None, "font", "font1-32.fxspr"),
    ("font1", 64, r"enginefx\font_simp-64\filelist_font1.txt", None, "font", "font1-64.fxspr"),
]

# Texture sheets: pair textures-32 (baseline) with textures-128 (higher-res).
TEXTURE_32_DIR = r"enginefx\textures-32"
TEXTURE_128_DIR = r"enginefx\textures-128"


def count_entries(fxspr_path):
    if not os.path.isfile(fxspr_path):
        return None
    blob = open(fxspr_path, "rb").read()
    count, _entries, _payload, _hdr = p2f.fx._parse_v2(blob)
    return count


def filelist_len(path):
    n = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.strip() and not line.startswith("#"):
                n += 1
    return n


def pack_one(filelist, image_root, out_path, scale, category, display_name,
             parity_ref=None):
    n, payload, missing = p2f.convert(
        filelist, out_path, image_root=image_root, scale=scale,
        provenance="keeperfx", category=category, display_name=display_name)
    status = "OK"
    if parity_ref is not None:
        want = count_entries(parity_ref)
        if want is not None and want != n:
            status = f"MISALIGNED (indexed={want} truecolour={n}) -> SKIPPED"
            os.remove(out_path)
            return False, n, missing, status
    warn = f" ({missing} missing)" if missing else ""
    print(f"  {os.path.basename(out_path):<22} scale={scale:<4} "
          f"entries={n:<5} payload={payload}B{warn}  [{status}]")
    return True, n, missing, status


def discover_texture_pairs(fx_root):
    d32 = os.path.join(fx_root, TEXTURE_32_DIR)
    d128 = os.path.join(fx_root, TEXTURE_128_DIR)
    pairs = []
    if not (os.path.isdir(d32) and os.path.isdir(d128)):
        return pairs
    for name in sorted(os.listdir(d128)):
        if not (name.startswith("filelist_") and name.endswith(".txt")):
            continue
        sheet = name[len("filelist_"):-len(".txt")]
        f32 = os.path.join(d32, name)
        f128 = os.path.join(d128, name)
        if not os.path.isfile(f32):
            continue
        if filelist_len(f32) != filelist_len(f128):
            print(f"  [skip texture] {sheet}: 32/128 entry counts differ")
            continue
        pairs.append((sheet, f32, f128))
    return pairs


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    ap = argparse.ArgumentParser(description="pack FXGraphics higher-quality tiers")
    ap.add_argument("--fx-root", default=os.path.join(
        os.path.dirname(repo), "FXGraphics"),
        help="FXGraphics repo root (default: sibling of this repo)")
    ap.add_argument("--out-dir", default=os.path.join(repo, ".deploy", "data", "fxspr"))
    ap.add_argument("--no-textures", action="store_true",
                    help="skip the higher-res texture tiers")
    args = ap.parse_args()

    if not os.path.isdir(args.fx_root):
        sys.exit(f"FXGraphics not found at {args.fx_root} (pass --fx-root)")
    os.makedirs(args.out_dir, exist_ok=True)

    made = 0
    print(f"[fxgraphics] source: {args.fx_root}")
    print("truecolour re-packs (aligned to indexed sheets):")
    for base, scale, fl, iroot, cat, ref in GUI_FONT_VARIANTS:
        filelist = os.path.join(args.fx_root, fl)
        if not os.path.isfile(filelist):
            print(f"  [skip] {base}-{scale}: {fl} not found")
            continue
        image_root = os.path.join(args.fx_root, iroot) if iroot else None
        out = os.path.join(args.out_dir, f"{base}-{scale}-tc.fxspr")
        ref_path = os.path.join(args.out_dir, ref) if ref else None
        ok, *_ = pack_one(filelist, image_root, out, scale, cat, base, ref_path)
        made += 1 if ok else 0

    if not args.no_textures:
        print("higher-res texture tiers (32 baseline + 128 upscaled):")
        for sheet, f32, f128 in discover_texture_pairs(args.fx_root):
            out32 = os.path.join(args.out_dir, f"{sheet}-32-tc.fxspr")
            out128 = os.path.join(args.out_dir, f"{sheet}-128-tc.fxspr")
            ok32, *_ = pack_one(f32, None, out32, 32, "texture", sheet)
            ok128, *_ = pack_one(f128, None, out128, 128, "texture", sheet)
            made += (1 if ok32 else 0) + (1 if ok128 else 0)

    print(f"[fxgraphics] wrote {made} truecolour .fxspr into {args.out_dir}")
    print("next: python tools/fxspr/make_manifest.py  (regenerate base.json)")


if __name__ == "__main__":
    main()
