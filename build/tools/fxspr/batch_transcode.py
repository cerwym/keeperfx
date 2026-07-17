#!/usr/bin/env python3
"""Batch bootstrap: transcode every indexed .dat/.tab sheet in a data dir to .fxspr.

Wraps build/tools/fxspr/dat_to_fxspr.py (the single-sheet bootstrap transcoder) and runs
it over all `.dat` files that have a matching `.tab`, writing `<stem>.fxspr` into the
output directory. This produces the full companion set the engine loads alongside the
indexed sheets (SpriteSheetManager derives `fxspr/<stem>.fxspr` from the .dat basename).

Bootstrap caveat: the RGBA it emits is *palette* colour (the legacy art quantised
against the supplied palette), not true 24-bit source colour. That is fine for wiring
and testing the truecolour render path end-to-end; the real pngpal2raw `-f fxspr`
branch will later emit the same container straight from 24-bit source PNGs.

Since the engine now treats .fxspr sprites as palette-INDEPENDENT (they are not
re-baked when the active palette changes), pick a palette that represents the art's
intended colours. `palette.dat` (the in-game base palette) is the sensible default.
"""
import argparse
import os
import sys

# Reuse the single-sheet transcoder living next to this script.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dat_to_fxspr as t  # noqa: E402


def find_pairs(data_dir):
    """Yield (stem, dat_path, tab_path) for every .dat with a sibling .tab."""
    for name in sorted(os.listdir(data_dir)):
        if not name.lower().endswith(".dat"):
            continue
        stem = name[:-4]
        dat_path = os.path.join(data_dir, name)
        tab_path = os.path.join(data_dir, stem + ".tab")
        if os.path.isfile(tab_path):
            yield stem, dat_path, tab_path


def infer_scale(stem):
    """Infer the nominal scale tier from a stem suffix like '-32' / '-64'."""
    for tier in (128, 64, 32):
        if stem.endswith(f"-{tier}") or stem.endswith(str(tier)):
            return tier
    return 0


def infer_category(stem):
    s = stem.lower()
    if s.startswith("font"):
        return "font"
    if s.startswith("pointer") or s.startswith("points"):
        return "pointer"
    if s.startswith("gui"):
        return "gui"
    return "unknown"


def main():
    ap = argparse.ArgumentParser(description="batch legacy .dat/.tab -> .fxspr v2")
    ap.add_argument("--data-dir", default=".deploy/data",
                    help="directory holding the .dat/.tab pairs (default: .deploy/data)")
    ap.add_argument("--pal", default=None,
                    help="palette file (default: <data-dir>/palette.dat)")
    ap.add_argument("--out-dir", default=None,
                    help="output dir for .fxspr (default: <data-dir>/fxspr)")
    ap.add_argument("--only", nargs="*", default=None,
                    help="optional list of stems to limit to (e.g. gui1-32 gui2-32)")
    ap.add_argument("--provenance", default="bullfrog",
                    help="provenance tag stamped into every sheet (default: bullfrog)")
    ap.add_argument("--verify", action="store_true",
                    help="run the parity self-check on each written file")
    args = ap.parse_args()

    pal = args.pal or os.path.join(args.data_dir, "palette.dat")
    out_dir = args.out_dir or os.path.join(args.data_dir, "fxspr")
    os.makedirs(out_dir, exist_ok=True)

    if not os.path.isfile(pal):
        raise SystemExit(f"palette not found: {pal}")

    pairs = list(find_pairs(args.data_dir))
    if args.only:
        wanted = set(args.only)
        pairs = [p for p in pairs if p[0] in wanted]
    if not pairs:
        raise SystemExit(f"no .dat/.tab pairs found under {args.data_dir}")

    total_ok = 0
    total_entries = 0
    for stem, dat_path, tab_path in pairs:
        out_path = os.path.join(out_dir, stem + ".fxspr")
        try:
            n, payload = t.transcode(
                tab_path, dat_path, pal, out_path,
                scale=infer_scale(stem), provenance=args.provenance,
                colour_mode="indexed", category=infer_category(stem),
                display_name=stem)
            if args.verify:
                t.verify(out_path, tab_path)
            total_ok += 1
            total_entries += n
            print(f"[ok]   {stem:<12} {n:>5} entries  {payload:>9} B payload -> {out_path}")
        except Exception as exc:  # noqa: BLE001 - report and keep going
            print(f"[fail] {stem:<12} {exc}")

    print(f"\n[batch] {total_ok}/{len(pairs)} sheets transcoded, {total_entries} total entries, "
          f"palette={pal}, out={out_dir}")
    return 0 if total_ok == len(pairs) else 1


if __name__ == "__main__":
    sys.exit(main())
