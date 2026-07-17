# Sprite rotational fidelity — format amendment + engine support

Status: design landed; format structs + engine parser compiled but **unused** by
the draw path. Nothing changes visually yet.

## 1. How the engine uses rotation data today

Creature sprites are pre-rendered bitmaps, not models. "Rotation" is a
render-time selection of one pre-baked facing, optionally horizontally mirrored.
The angle is an 11-bit fixed-point value: `0..2047 == 0..360°`
(`ANGLE_MASK = 2047`, `DEGREES_22_5 = 128`, i.e. `2048/16`).

Every rotation site uses the identical math:

```
sector = ((angle + DEGREES_22_5) & ANGLE_MASK) >> 8   // 0..7, round to nearest 45°
group  = abs(4 - sector)                              // fold 8 sectors -> 5 stored groups
xflip  = (angle > DEGREES_202_5=1151) && (angle < DEGREES_337_5=1919)  // left half mirrored
kspr   = &base[ frame + group * FramesCount ]
```

Three sites, all in lock-step:
- `engine_render.c` `draw_keepersprite` (~7947–8038) — the actual world draw.
- `engine_render.c` `draw_keepsprite_unscaled_in_buffer` (~8438–8514) — creature-view / unscaled path.
- `creature_graphics.c` `get_keepsprite_unscaled_dimensions` (~292–329) — dimension query.

`Rotable` (per `KeeperSprite`) selects the mode:
- `0` — flat / faces camera. One group of `FramesCount`. (Still x-flipped by hemisphere in some paths.)
- `2` — directional. `FramesCount * 5` contiguous entries: group 0=0°(front), 1=45°, 2=90°(side), 3=135°, 4=180°(back). Facings 225/270/315 are the mirror of 135/90/45.
- `1` — not handled by any draw path (legacy/unused).

On-disk `KeeperSpriteDisk` (16 B, `creature.tab`) is unpacked 1:1 into
`KeeperSprite` (`creature_table_load_unpack`). Per-frame pivot is `offset_x`,
`offset_y` (plus `FrameWidth/Height`, `FrameOffsW/H`).

**Key conclusion:** the source carries *no* extra angular data. The 8-facing look
is produced from **5 stored bitmaps + mirror**. More fidelity ⇒ **more stored
bitmaps** (new art), plus a way to describe how facings map to them. So the
amendment is about *describing and carrying* arbitrary direction sets, not about
squeezing hidden angles out of the existing data.

## 2. Design goals

1. An animation can declare an **arbitrary number of directions** at **arbitrary
   angles**, each either a real stored bitmap or a **mirror** of another.
2. Per-frame pivot stays per-entry (already true) and may differ per direction.
3. **Forward compatible**: current v2 readers and the current engine keep working
   unchanged on both legacy and extended files.
4. The engine can **parse and carry** the richer directionality but the draw path
   keeps using the legacy 5-group math until we explicitly flip the switch.

## 3. Format amendment (.fxspr v2, additive)

The container already locates every block by **absolute byte offset** from file
start (`directory_off`, `payload_off`, `assetinfo_off`, `stringtable_off`), and
readers dispatch on `version`. That is the forward-compat lever: a new block can
be appended and old readers never look at it.

Additions (all in `FxSprFormat.h`, the single source of truth):

- New flag bit `FxSprFlag_AnimBlock = 0x0020`.
- New fixed-position header extension `FxSprHeaderExt3` (16 B) at file offset 48
  (immediately after `FxSprHeaderExt2`), **present iff** `FxSprFlag_AnimBlock`.
  Old readers only ever consume the first 48 bytes then jump by absolute offsets,
  so they ignore Ext3 and the anim block entirely. New readers check the flag.
- New optional **animation descriptor block**, located by `Ext3.animblock_off`:
  - `FxSprAnimBlock` header: `version`, `anim_stride`, `dir_stride`,
    `anim_count`, `dir_count`. Strides make each record independently growable
    (readers advance by the stride and read only fields they know).
  - `FxSprAnim[anim_count]`: `group_id` (matches `FxSprEntryRich.group_id`),
    `name_off`, `frames` (per direction), `dir_first`, `dir_count`,
    `legacy_rotable` (0/1/2, for the unchanged draw path), `view`, `fps`.
  - `FxSprAnimDir[dir_count]`: `angle` (0..2047 this bitmap represents),
    `mirror` (0/1), `base_dir` (dir index within this anim to mirror when
    `mirror`, else self), `entry_first` (directory index of this direction's
    frame 0; frames are `entry_first .. entry_first+frames-1`).

`FxSprEntryRich.rotation` is (re)documented as **direction index within the
animation**; `frame_index` as position within a direction. Pixels stay 1:1 with
entries, so adding directions is just adding entries + dir records — no break.

Legacy mapping: a Rotable-2 animation is expressible as either 5 real dirs
(0,256,512,768,1024) with the engine applying the mirror, or 8 explicit dirs
(5 real + 3 `mirror`). A higher-fidelity asset is 16 dirs (every 128 ≈ 22.5°) or
32 dirs (every 64 ≈ 11.25°), etc. Non-uniform sets are allowed.

## 4. Engine support (compiled, not used)

- `FxSprAnimBlock.h` mirrors the on-disk structs (pack(1)), shared by loader and
  tools, with `_Static_assert` size checks like the rest of the format.
- Loader `FxSprAnimTable` (new): given an inflated `.fxspr` with the anim flag,
  parse the block into engine-side tables:
  - `KfxAnim { anim_id, frames, view, legacy_rotable, dir_first, dir_count }`
  - `KfxAnimDir { angle, mirror, base_dir, entry_first }`
- Generalized selector (new, pure, unit-testable), **not called by the renderer**:
  `int kfx_anim_select_dir(const KfxAnim*, const KfxAnimDir*, int angle,
   int* out_mirror)` — nearest-angle search over the anim's dirs (wrapping the
   circle), resolving `mirror`/`base_dir`. When no extended table exists it
   returns the legacy `abs(4 - sector)` result, so it is a strict superset.
- The three render sites are **left untouched**. A follow-up will switch them to
  `kfx_anim_select_dir` behind a config/flag (default off) once runtime
  consumption from `.fxspr` lands.

## 5. Authoring / migration

- `jty_to_fxspr.py` already emits `creature.anim.json`. Formalize its schema to
  carry per-direction `{angle, mirror, base_dir, entry_first}` and add an option
  to serialize the binary anim block into the `.fxspr`.
- A future high-fidelity pack supplies N-direction art; the packer writes N dir
  records. No format break; old engine renders the 5 it understands, new engine
  (once switched on) renders all N.

## 6. Why forward compatible (summary)

- Absolute offsets + `version` dispatch ⇒ appended blocks are invisible to old
  readers.
- New flag bit gates the new header extension; unset ⇒ file is exactly today's v2.
- Per-record strides ⇒ future fields don't shift existing ones.
- Draw path unchanged ⇒ zero behavioural risk until deliberately enabled.
