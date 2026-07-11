#!/usr/bin/env python3
"""Generate the BASE sprite-pack manifest from a directory of .fxspr v2 files.

A manifest declares, for the engine's VariantCatalogue, which logical sprite
each on-disk entry maps to and which quality variants exist. To scale to sheets
with hundreds of entries we describe COLLECTIONS rather than enumerating every
entry:

  * A collection groups the scale/colour variants of one logical sheet (e.g.
    gui1 at 32 and 64). The engine synthesises a stable id "<base>/<entry>" for
    every entry index, and resolves each variant as (file, entry=index).
  * An optional `sprites` list carries per-entry OVERRIDES only where they add
    information: a human-readable name, tags, or extra variants from a mod pack.

Variants are grouped by stripping a trailing scale suffix from the file stem:
  gui1-32.fxspr + gui1-64.fxspr  -> collection "gui1", scales {32,64}
  pointer64.fxspr                -> collection "pointer", scale {64}
  swipe01.fxspr                  -> collection "swipe01" (standalone)

Names are seeded from an optional JSON file (--names) mapping
"<base>/<entry>" -> {"name":..,"category":..,"tags":[..]}; this is the A4
name-catalogue hook. Anything not named still gets an id and is browsable.

Output is one pack JSON consumed by src/kfx/assets/VariantCatalogue.
"""
import argparse
import json
import os
import re
import struct
import zlib

FXSPR_HEADER = struct.Struct("<4sHHHHIII")
FXSPR_EXT2 = struct.Struct("<HHIIIII")

CATEGORY_BY_PREFIX = [
    ("font", "font"),
    ("pointer", "pointer"),
    ("points", "pointer"),
    ("gui", "gui"),
    ("swipe", "effect"),
    ("gmap", "gui"),
    ("tmap", "texture"),
]

SCALE_SUFFIX = re.compile(r"^(?P<base>.+?)-?(?P<scale>32|64|128)$")


def read_fxspr_meta(path):
    """Return (version, entry_count, colour_mode) from a .fxspr header."""
    with open(path, "rb") as f:
        head = f.read(FXSPR_HEADER.size)
        magic, ver, flags, kind, _res, count, dir_off, pay_off = FXSPR_HEADER.unpack(head)
        if magic != b"FXSP":
            raise ValueError(f"{path}: bad magic {magic!r}")
        colour = "indexed"
        if ver >= 2:
            ext = FXSPR_EXT2.unpack(f.read(FXSPR_EXT2.size))
            ai_off = ext[2]
            if ai_off:
                f.seek(ai_off)
                ai = f.read(8)
                # scale u16, colour_mode u8, provenance u8, ...
                _scale, cmode, _prov = struct.unpack("<HBB", ai[:4])
                colour = "truecolour" if cmode == 1 else "indexed"
        return ver, count, colour


def split_base_scale(stem):
    # optional trailing colour marker (-tc/-idx); the colour mode itself is read
    # from the file header, so it only needs stripping to recover the base.
    for marker in ("-tc", "-idx"):
        if stem.endswith(marker):
            stem = stem[: -len(marker)]
            break
    m = SCALE_SUFFIX.match(stem)
    if m and (stem.endswith("-" + m.group("scale"))):
        return m.group("base"), int(m.group("scale"))
    # trailing digits that are NOT a scale suffix (e.g. swipe01, pointer64)
    # treat pointerNN/pointsNN where NN is a scale tier
    if stem.startswith(("pointer", "points")) and stem[-2:] in ("32", "64"):
        return stem[:-2], int(stem[-2:])
    return stem, 0


def category_for(base):
    for prefix, cat in CATEGORY_BY_PREFIX:
        if base.startswith(prefix):
            return cat
    return "unknown"


def build_manifest(fxspr_dir, names_catalogue, pack_name, provenance, priority):
    groups = {}  # base -> {"category":.., "count":N, "variants":[...]}
    for name in sorted(os.listdir(fxspr_dir)):
        if not name.lower().endswith(".fxspr"):
            continue
        stem = name[:-6]
        path = os.path.join(fxspr_dir, name)
        try:
            _ver, count, colour = read_fxspr_meta(path)
        except Exception as exc:  # noqa: BLE001
            print(f"[skip] {name}: {exc}")
            continue
        base, scale = split_base_scale(stem)
        g = groups.setdefault(base, {"category": category_for(base),
                                     "count": 0, "variants": []})
        g["count"] = max(g["count"], count)
        g["variants"].append({
            "scale": scale,
            "colour": colour,
            "file": "fxspr/" + name,
            "entries": count,
        })

    collections = []
    for base in sorted(groups):
        g = groups[base]
        g["variants"].sort(key=lambda v: (v["scale"], v["colour"]))
        collections.append({
            "base": base,
            "category": g["category"],
            "entries": g["count"],
            "variants": [{"scale": v["scale"], "colour": v["colour"],
                          "file": v["file"]} for v in g["variants"]],
        })

    # per-entry name/category/tag overrides from the A4 name-catalogue
    sprites = []
    for sprite_id in sorted(names_catalogue):
        rec = names_catalogue[sprite_id]
        entry = {"id": sprite_id}
        if rec.get("name"):
            entry["name"] = rec["name"]
        if rec.get("category"):
            entry["category"] = rec["category"]
        if rec.get("tags"):
            entry["tags"] = rec["tags"]
        if rec.get("variants"):
            entry["variants"] = rec["variants"]
        sprites.append(entry)

    return {
        "pack": {"name": pack_name, "provenance": provenance, "priority": priority},
        "collections": collections,
        "sprites": sprites,
    }


def main():
    ap = argparse.ArgumentParser(description="generate the base sprite-pack manifest")
    ap.add_argument("--fxspr-dir", default=".deploy/data/fxspr",
                    help="dir of .fxspr v2 files to scan (default: .deploy/data/fxspr)")
    ap.add_argument("--out", default="config/fxdata/spritepacks/base.json")
    ap.add_argument("--names", default=None,
                    help="optional A4 name-catalogue JSON (id -> {name,category,tags})")
    ap.add_argument("--pack-name", default="base")
    ap.add_argument("--provenance", default="bullfrog",
                    choices=["unknown", "bullfrog", "keeperfx", "mod"])
    ap.add_argument("--priority", type=int, default=0)
    args = ap.parse_args()

    names = {}
    if args.names and os.path.isfile(args.names):
        names = json.load(open(args.names, "r", encoding="utf-8"))

    manifest = build_manifest(args.fxspr_dir, names, args.pack_name,
                              args.provenance, args.priority)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    ncol = len(manifest["collections"])
    nsp = len(manifest["sprites"])
    print(f"[manifest] wrote {args.out}: {ncol} collections, {nsp} named overrides")
    for c in manifest["collections"]:
        scales = ",".join(f"{v['scale']}{v['colour'][0]}" for v in c["variants"])
        print(f"  {c['base']:<12} {c['category']:<8} {c['entries']:>5} entries  [{scales}]")


if __name__ == "__main__":
    main()
