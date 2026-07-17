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
sprite-pack manifests. This tool emits:
  1) a formal sidecar descriptor (--anim-json), and
  2) an optional binary FxSprAnimBlock embedded in the .fxspr container.
colour_mode=indexed (palette-derived), provenance=bullfrog, category=creature,
kind=Animation.
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
    """Formal rotational-fidelity sidecar schema (v1) + anim-block source.

    Top-level shape:
      {
        "schema": "keeperfx.fxspr.anim.v1",
        "animations": [
          {group_id, frames, legacy_rotable, view, fps, directions:[{angle,mirror,base_dir,entry_first}]}
        ]
      }

    With creature.tab alone we can preserve legacy contiguous layout facts but
    cannot recover model-level animation names, so we emit one logical animation
    record per KeeperSprite entry (group_id=i). For Rotable==2, when the full
    5-group run is in-bounds, directions are emitted in explicit 8-facing form:
    5 real (0,45,90,135,180) + 3 mirrors (225,270,315).
    """
    recs = []
    total = len(table)
    for i, ks in enumerate(table):
        frames = max(1, int(ks["FramesCount"]))
        rotable = int(ks["Rotable"])

        dirs = [{
            "angle": 0,
            "mirror": 0,
            "base_dir": 0,
            "entry_first": i,
        }]
        if rotable == 2 and i + frames * 5 <= total:
            dirs = [
                {"angle": 0, "mirror": 0, "base_dir": 0, "entry_first": i + 0 * frames},
                {"angle": 256, "mirror": 0, "base_dir": 1, "entry_first": i + 1 * frames},
                {"angle": 512, "mirror": 0, "base_dir": 2, "entry_first": i + 2 * frames},
                {"angle": 768, "mirror": 0, "base_dir": 3, "entry_first": i + 3 * frames},
                {"angle": 1024, "mirror": 0, "base_dir": 4, "entry_first": i + 4 * frames},
                {"angle": 1280, "mirror": 1, "base_dir": 3, "entry_first": i + 3 * frames},
                {"angle": 1536, "mirror": 1, "base_dir": 2, "entry_first": i + 2 * frames},
                {"angle": 1792, "mirror": 1, "base_dir": 1, "entry_first": i + 1 * frames},
            ]

        recs.append({
            "group_id": i,
            "name": "",
            "frames": frames,
            "legacy_rotable": max(0, min(rotable, 255)),
            "view": fx.FXSPR_VIEW_TOPDOWN,
            "fps": 0,
            "directions": dirs,
        })

    dir_count = sum(len(a["directions"]) for a in recs)
    return {
        "schema": "keeperfx.fxspr.anim.v1",
        "version": 1,
        "source": "creature.tab",
        "entry_count": len(recs),
        "anim_count": len(recs),
        "dir_count": dir_count,
        "animations": recs,
    }


def convert(tab_path, jty_path, pal_path, out_path, anim_json=None,
            compress=True, write_anim_block=True):
    table = read_creature_tab(tab_path)
    jty = open(jty_path, "rb").read()
    pal = fx.load_palette(pal_path)
    sprites, decoded = build_sprites(table, jty, pal)
    anim = anim_descriptor(table)
    n, payload = fx.write_v2(
        out_path, sprites, scale=0, provenance="bullfrog",
        colour_mode="indexed", category="creature",
        display_name="creature", compress=compress,
        kind=fx.FXSPR_KIND_ANIMATION,
        anims=anim["animations"] if write_anim_block else None)
    if anim_json:
        os.makedirs(os.path.dirname(os.path.abspath(anim_json)), exist_ok=True)
        with open(anim_json, "w", encoding="utf-8") as f:
            json.dump(anim, f, indent=2)
    return n, payload, decoded, len(table), anim


def verify(out_path, tab_path, expected_anim=None):
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
        # empty sentinel entries are allowed to be 0x0
        if (w, h) not in ((0, 0), (ks["SWidth"], ks["SHeight"])):
            mism += 1
        span = w * h * 4
        for px in range(data_off + 3, data_off + span, 4):
            if payload[px] != 0:
                opaque += 1
    assert mism == 0, f"{mism} entries with dims not matching .tab"
    parsed_anim = fx.parse_anim_block(blob)
    if expected_anim is not None:
        assert parsed_anim is not None, "anim block missing"
        exp = expected_anim["animations"]
        got = parsed_anim["animations"]
        assert len(got) == len(exp), (len(got), len(exp))
        for i, (ga, ea) in enumerate(zip(got, exp)):
            assert ga["group_id"] == ea["group_id"], (i, ga["group_id"], ea["group_id"])
            assert ga["frames"] == ea["frames"], (i, ga["frames"], ea["frames"])
            assert ga["legacy_rotable"] == ea["legacy_rotable"], (
                i, ga["legacy_rotable"], ea["legacy_rotable"])
            assert ga["view"] == ea["view"], (i, ga["view"], ea["view"])
            assert ga["fps"] == ea["fps"], (i, ga["fps"], ea["fps"])
            assert ga["directions"] == ea["directions"], (
                i, ga["directions"], ea["directions"])
    else:
        assert parsed_anim is None, "anim block unexpectedly present"
    print(f"[verify] OK  kind=Animation entries={count} dim-mismatches=0 "
          f"raw_payload={len(payload)}B file={len(blob)}B opaque_px={opaque} "
          f"anim_block={'yes' if parsed_anim else 'no'}")


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
                    help="sidecar animation descriptor (keeperfx.fxspr.anim.v1)")
    ap.add_argument("--no-anim-block", action="store_true",
                    help="do not embed FxSprAnimBlock into the .fxspr")
    ap.add_argument("--no-compress", action="store_true")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    n, payload, decoded, total, anim = convert(
        args.tab, args.jty, args.pal, args.out,
        anim_json=args.anim_json, compress=not args.no_compress,
        write_anim_block=not args.no_anim_block)
    print(f"[creature] wrote {args.out}: {n} entries "
          f"({decoded} decoded, {total - decoded} empty), payload={payload}B")
    if args.verify:
        verify(args.out, args.tab, expected_anim=anim if not args.no_anim_block else None)


if __name__ == "__main__":
    main()
