#!/usr/bin/env python3
"""Convert Bullfrog's creature.jty / creature.tab into a v2 .fxspr (Animation).

This is the hard arm of the asset pipeline: creature animation frames live in a
16MB RLE blob (creature.jty) indexed by a flat KeeperSprite directory
(creature.tab), not the GUI .dat/.tab shape. This tool replicates the engine's
offline-safe decode (src/kfx/imgui/CreatureSpriteCache.cpp) in Python and emits a
single self-describing creature.fxspr whose entries mirror creature_table 1:1:
entry i == KeeperSprite index i, so a SpriteHandle maps across the indexed and
.fxspr worlds exactly like the flat sheets.

On-disk creature.tab is an array of KeeperSpriteDisk (creature_graphics.h):
    uint32 DataOffset
    uint8  SWidth, SHeight, FrameWidth, FrameHeight
    uint8  Rotable, FramesCount, FrameOffsW, FrameOffsH
    int16  offset_x, offset_y
= 16 bytes, pack(1). creature_graphics.c::creature_table_load_unpack widens it to
struct KeeperSprite (shadow_offset / frame_flags default to 0).

Each frame's pixels are creature.jty RLE at DataOffset, SWidth x SHeight, decoded
with the KeeperSprite convention (literal index 0 stays 0 -> transparent, NO
0->1 remap; mirrors CreatureSpriteCache) and expanded to RGBA8 through the game
palette. Invalid / zero-dim entries become empty sentinels so the 1:1 index
mapping is preserved.

The rich entry carries the render-critical KeeperSprite fields (dims, offset_x,
offset_y, rotation=Rotable). The animation GROUPING (which kspr runs form which
creature model / animation, FramesCount playback, TD vs FP views) is identity
metadata that belongs in the manifest layer (VariantCatalogue), exactly like the
sprite-pack manifests -- so it is emitted as an optional sidecar descriptor
(--anim-json) rather than bloating every pixel entry. colour_mode=indexed
(palette-derived), provenance=bullfrog, category=creature, kind=Animation.
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dat_to_fxspr as fx  # noqa: E402

# KeeperSpriteDisk, pack(1): see creature_graphics.h struct KeeperSpriteDisk.
KSPR_DISK = struct.Struct("<IBBBBBBBBhh")  # 16 bytes
assert KSPR_DISK.size == 16

MAX_DIM = 256  # matches CreatureSpriteCache k_max_dim / engine scratch


def read_creature_tab(path):
    """Return a list of KeeperSprite dicts in .tab order."""
    data = open(path, "rb").read()
    if len(data) % KSPR_DISK.size != 0:
        raise SystemExit(f"{path}: size {len(data)} not a multiple of 16")
    out = []
    for i in range(len(data) // KSPR_DISK.size):
        (data_off, sw, sh, fw, fh, rot, frames, fow, foh,
         ox, oy) = KSPR_DISK.unpack_from(data, i * KSPR_DISK.size)
        out.append({
            "data_off": data_off, "SWidth": sw, "SHeight": sh,
            "FrameWidth": fw, "FrameHeight": fh, "Rotable": rot,
            "FramesCount": frames, "FrameOffsW": fow, "FrameOffsH": foh,
            "offset_x": ox, "offset_y": oy,
        })
    return out


def dims_ok(ks):
    return 0 < ks["SWidth"] <= MAX_DIM and 0 < ks["SHeight"] <= MAX_DIM


def build_sprites(table, jty, pal):
    sprites = []
    decoded = 0
    jty_len = len(jty)
    for i, ks in enumerate(table):
        if not dims_ok(ks) or ks["data_off"] >= jty_len:
            sprites.append({"w": 0, "h": 0, "rgba": b"", "group_id": i})
            continue
        w, h = ks["SWidth"], ks["SHeight"]
        # Row/height self-terminating RLE; bound the read to the file tail.
        idx = fx.decode_rle(jty, ks["data_off"], jty_len - ks["data_off"],
                            w, h, zero_to_one=False)
        rgba = fx.indices_to_rgba(idx, w, h, pal)
        sprites.append({
            "w": w, "h": h, "rgba": bytes(rgba),
            "offset_x": ks["offset_x"], "offset_y": ks["offset_y"],
            "rotation": min(ks["Rotable"], 255),
            "group_id": i, "frame_index": 0,
            "view": fx.FXSPR_VIEW_TOPDOWN,
        })
        decoded += 1
    return sprites, decoded


def anim_descriptor(table):
    """The animation-grouping metadata that does not belong in per-pixel entries.
    One record per kspr index; the future per-model layer / VariantCatalogue
    consumes this to reconstruct animations without re-reading the .tab."""
    recs = []
    for i, ks in enumerate(table):
        recs.append({
            "kspr": i, "w": ks["SWidth"], "h": ks["SHeight"],
            "frame_w": ks["FrameWidth"], "frame_h": ks["FrameHeight"],
            "rotable": ks["Rotable"], "frames": ks["FramesCount"],
            "frame_offs_w": ks["FrameOffsW"], "frame_offs_h": ks["FrameOffsH"],
            "offset_x": ks["offset_x"], "offset_y": ks["offset_y"],
        })
    return {"source": "creature.tab", "count": len(recs), "entries": recs}


def convert(tab_path, jty_path, pal_path, out_path, anim_json=None,
            compress=True):
    table = read_creature_tab(tab_path)
    jty = open(jty_path, "rb").read()
    pal = fx.load_palette(pal_path)
    sprites, decoded = build_sprites(table, jty, pal)
    n, payload = fx.write_v2(
        out_path, sprites, scale=0, provenance="bullfrog",
        colour_mode="indexed", category="creature",
        display_name="creature", compress=compress,
        kind=fx.FXSPR_KIND_ANIMATION)
    if anim_json:
        os.makedirs(os.path.dirname(os.path.abspath(anim_json)), exist_ok=True)
        with open(anim_json, "w", encoding="utf-8") as f:
            json.dump(anim_descriptor(table), f)
    return n, payload, decoded, len(table)


def verify(out_path, tab_path):
    """Structural parity: entry count + per-entry dims must match the .tab."""
    table = read_creature_tab(tab_path)
    blob = open(out_path, "rb").read()
    count, entries, payload, (flags, kind, _info) = fx._parse_v2(blob)
    assert kind == fx.FXSPR_KIND_ANIMATION, f"kind {kind} != Animation"
    assert count == len(table), (count, len(table))
    mism = 0
    opaque = 0
    for i, (data_off, w, h, *_rest) in enumerate(entries):
        ks = table[i]
        exp_w = ks["SWidth"] if dims_ok(ks) and ks["data_off"] < 0xFFFFFFFF else 0
        # empty sentinel entries are allowed to be 0x0
        if (w, h) not in ((0, 0), (ks["SWidth"], ks["SHeight"])):
            mism += 1
        span = w * h * 4
        for px in range(data_off + 3, data_off + span, 4):
            if payload[px] != 0:
                opaque += 1
    assert mism == 0, f"{mism} entries with dims not matching .tab"
    print(f"[verify] OK  kind=Animation entries={count} dim-mismatches=0 "
          f"raw_payload={len(payload)}B file={len(blob)}B opaque_px={opaque}")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", ".."))
    data = os.path.join(repo, ".deploy", "data")
    ap = argparse.ArgumentParser(description="creature.jty/.tab -> creature.fxspr (Animation)")
    ap.add_argument("--tab", default=os.path.join(data, "creature.tab"))
    ap.add_argument("--jty", default=os.path.join(data, "creature.jty"))
    ap.add_argument("--pal", default=os.path.join(data, "palette.dat"))
    ap.add_argument("--out", default=os.path.join(data, "fxspr", "creature.fxspr"))
    ap.add_argument("--anim-json", default=os.path.join(data, "fxspr", "creature.anim.json"),
                    help="sidecar animation descriptor (grouping metadata)")
    ap.add_argument("--no-compress", action="store_true")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    n, payload, decoded, total = convert(
        args.tab, args.jty, args.pal, args.out,
        anim_json=args.anim_json, compress=not args.no_compress)
    print(f"[creature] wrote {args.out}: {n} entries "
          f"({decoded} decoded, {total - decoded} empty), payload={payload}B")
    if args.verify:
        verify(args.out, args.tab)


if __name__ == "__main__":
    main()
