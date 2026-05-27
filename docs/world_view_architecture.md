# World View Architecture

This document describes the complete rendering architecture for the KeeperFX isometric world view — from scene construction through to final pixels on screen — covering both the original software path and the OpenGL hardware path.

---

## Overview

The world view renders the 3D dungeon as a 2.5D scene composed of:

- **Tile geometry** — column stacks (floor + wall cubes + ceiling) per map subtile
- **3D entity sprites** — creatures, objects, doors, traps (billboarded keeper-sprites)
- **Creature shadows** — silhouette quads projected onto the floor plane
- **Flat-colour polygons** — power possession overlay, possession tunnel geometry
- **Overlay elements** — status flowers, gold text, room flags, slab selector lines

Every frame, the scene is built in two stages: a **scene-build phase** that runs entirely on the game thread and is shared between both backends, and a **render phase** that differs completely between software and hardware.

---

## Stage 1: Scene Construction (shared, platform-neutral)

The scene-build phase runs from `draw_view()` → `WorldViewRenderer_DrawIsometricView()` and is identical regardless of whether software or OpenGL rendering is active.

### 1.1 Camera setup

`draw_view()` (`src/engine_render.c`) establishes the camera transform for the frame:

```c
init_coords_and_rotation(&object_origin, &camera_matrix);
rotate_base_axis(&camera_matrix, cam->rotation_angle_x, 2);   // yaw
rotate_base_axis(&camera_matrix, -cam->rotation_angle_y, 1);  // pitch
rotate_base_axis(&camera_matrix, -cam->rotation_angle_z, 3);  // roll
```

`camera_matrix` is a 3×3 rotation matrix stored in 16.14 fixed-point (`struct M33`). It encodes the camera orientation in world space and is used by every vertex projection call that follows.

Global scalars derived from `cam->zoom` and the viewport dimensions are also set here: `camera_zoom`, `view_width_over_2`, `view_height_over_2`, `cells_away`, `fade_min`, `fade_max`.

### 1.2 Frustum gamut (visible tile range)

`fiddle_gamut()` computes the `minmaxs[]` array — a per-row min/max X range in subtile space. This defines which subtiles are potentially visible and need to have their vertices projected. The algorithm differs by view mode:

- **Isometric / Wibble (`PVM_IsoWibbleView`, `PVM_IsoStraightView`)**: projects screen corners to world, clips to a circular radius, then calls `fiddle_gamut_set_minmaxes()` to rasterise the view frustum edges into per-row min/max limits.
- **First-person (`PVM_CreatureView`)**: uses `fiddle_half_gamut()`, which walks outward from the camera position and clips at solid walls in each direction.

### 1.3 Vertex projection

`fill_in_points_isometric()` (or `fill_in_points_cluedo()` in low-wall mode, `fill_in_points_perspective()` in first-person mode) iterates every subtile within the gamut and fills two `EngineCol` arrays — `back_ec[]` (the far edge of each subtile column) and `front_ec[]` (the near edge).

Each `EngineCoord` in the array gets its world-space position set, then `rotpers()` (or `rotpers_parallel_3()` in the isometric fast path) is called to produce:

- `ecord->view_width` / `ecord->view_height` — integer screen pixel coordinates
- `ecord->z` — camera-space depth, clamped to `[0, Z_DRAW_DISTANCE_MAX]`
- `ecord->clip_flags` — frustum clip bits (left/right/top/bottom/near)
- `ecord->shade_intensity` — per-vertex lightness sampled from `game.lish`

Wibble offsets (animated vertex displacement for water/lava) are applied before projection.

### 1.4 Triangle submission into the bucket list

`do_a_plane_of_engine_columns_isometric()` (or `_cluedo`, `_perspective`) iterates the projected column stacks and emits triangles for each visible face:

- **Wall faces** — each cube face visible to camera (solidmask of neighbour is 0) emits two triangles via `do_a_gpoly_gourad_tr()` and `do_a_gpoly_gourad_bl()`.
- **Floor** — lowest solid level or floor texture, two triangles.
- **Top cap** — topmost solid cube, two triangles using `texture_id[4]`.
- **Ceiling** — universal ceiling at height 8 above the column.

Each `do_a_*` call:
1. Performs a backface cull (cross-product sign test on screen vertices).
2. Frustum-culls using the aggregate `clip_flags` AND.
3. Computes `bucket_idx = max(z0, z1, z2) / BUCKETS_STEP`.
4. Allocates a `BucketKindPolygonStandard` or `BucketKindPolygonNearFP` from the bump allocator `getpoly`.
5. Stores `PolyPoint` vertices (screen X/Y, UV in 16.16, shade in 16.8) and the `block` (tile texture index).
6. Prepends to `buckets[bucket_idx]` — a per-bucket singly-linked list.

Near-plane triangles (any vertex with camera Z < 32) are split into sub-triangles clipped against the near plane and stored as `QK_PolygonNearFP`.

### 1.5 Thing (sprite/shadow) injection

As `do_a_plane_of_engine_columns_isometric()` visits each map subtile, it calls `do_map_who()` for every thing on that subtile. The thing's position is projected, and the appropriate bucket item is allocated:

| Thing type | Bucket kind |
|---|---|
| Creatures, objects, doors, traps | `BucketKindJontySprite` (`QK_JontySprite`) |
| Spinning keys | `BucketKindJontySprite` (`QK_JontyISOSprite`) |
| Creature shadows | `BucketKindCreatureShadow` (`QK_CreatureShadow`) |
| Creature status flowers | `BucketKindCreatureStatus` (`QK_CreatureStatus`) |
| Gold/price numbers | `BucketKindFloatingGoldText` (`QK_FloatingGoldText`) |
| Room flags | `BucketKindRoomFlag` (`QK_RoomFlagBottomPole`) |
| Selection box lines | `BucketKindSlabSelector` (`QK_SlabSelector`) |

Shadow quads (`create_shadows()`) require additional work: the four corner vertices of the shadow projection are calculated analytically from the light position and the thing's sprite dimensions, then projected via `rotpers()` and stored with sprite metadata (`anim_sprite`, `angle`, `current_frame`).

### 1.6 Flat-colour polygons

Power-related overlays (possession tunnel, possession aura) call `add_flatpoly_to_polypool()` which stores `QK_PolyMode0 / QK_BasicPolygon` items. These carry screen-space vertex positions and a flat colour but no texture.

---

## Stage 2A: Software Rendering Path

### Entry point

`WorldViewRenderer_DrawIsometricView()` → `SoftwareWorldViewRenderer::DrawIsometricView()` → `display_drawlist()` (`src/engine_render.c`)

### 2A.1 Bucket traversal

The software renderer iterates `buckets[BUCKETS_COUNT-1]` down to `buckets[1]` (back-to-front, painter's algorithm). Bucket 0 is skipped because near-plane triangles produce degenerate scanlines in the software rasterizer.

For each bucket, items in the linked list are processed in insertion order (LIFO within the bucket — items are prepended on submission).

### 2A.2 Triangle rasterization

| Bucket kind | Handler | Description |
|---|---|---|
| `QK_PolygonStandard` / `QK_PolygonSimple` | `draw_gpoly()` | CPU textured triangle rasterizer |
| `QK_PolygonNearFP` | `draw_gpoly()` with near-clip | Near-plane split triangles |
| `QK_TrigMode2/3/6` | `trig()` variants | Compact triangle formats |
| `QK_PolyMode0/4/5` | `draw_gpoly()` | Flat/gouraud polygons |

`draw_gpoly()` (`src/bflib_render_gpoly.c`) dispatches to a mode-specific scanline function (`trig_gt()`, `trig_fl()`, etc.) based on `vec_mode`. These scanline functions interpolate U, V, and S (shade) per pixel, perform table-driven palette-indexed texture sampling, apply per-pixel shading from the fade/lighting LUT, and write directly to the CPU framebuffer (`WScreen`).

### 2A.3 Sprite rendering

`QK_JontySprite` items call `draw_jonty_mapwho()` → `draw_keepersprite()`, which is a CPU software sprite blitter. It scales and blits palette-indexed RLE-compressed keeper-sprites directly to the CPU framebuffer using pre-computed shade tables.

`QK_CreatureShadow` items call `draw_creature_shadow()` which blits a translucency-blended dark quad.

### 2A.4 Output

The software renderer writes directly into the locked CPU framebuffer. No intermediate FBO is involved. The result is a palettized 8bpp image which is composited with the UI and converted to the display surface by the platform layer.

---

## Stage 2B: OpenGL Hardware Rendering Path

### Architecture overview

The OpenGL path replaces `display_drawlist()` with a deferred, multi-pass GPU pipeline. The bucket list built in Stage 1 is consumed during `GLWorldViewRenderer::DrawIsometricView()` (still game-thread), which builds a set of **draw command lists** (`m_draw_cmds`, `m_shadow_cmds`, `m_flatpoly_verts`) and batches all tile/polygon vertices into a flat CPU array (`m_verts`). The actual GL calls are deferred to `GPURenderNow()`, which runs on the render thread after `FlipBuffers()` has moved these lists into `m_rt_*` shadow copies.

### Double-buffer protocol (Phase 3B)

| Buffer name | Written by | Read by |
|---|---|---|
| `m_draw_cmds` | game thread (DrawIsometricView) | `FlipBuffers()` only |
| `m_shadow_cmds` | game thread | `FlipBuffers()` only |
| `m_flatpoly_verts` | game thread | `FlipBuffers()` only |
| `m_verts` / `m_vert_count` | game thread | `FlipBuffers()` only |
| `m_rt_draw_cmds` | `FlipBuffers()` (std::move) | render thread (gpu_execute_passes) |
| `m_rt_shadow_cmds` | `FlipBuffers()` (std::move) | render thread |
| `m_rt_flatpoly_verts` | `FlipBuffers()` (std::move) | render thread |
| `m_rt_verts` / `m_rt_vert_count` | `FlipBuffers()` (std::swap) | render thread |

`FlipBuffers()` is called by `RendererOpenGL::EndFrame()` while the render thread is idle (before signalling it). This is a zero-copy swap: the vectors are `std::move`d (O(1)), and the raw vertex buffer is `std::swap`ped. After `FlipBuffers()`, the game thread resets `m_draw_cmds`, `m_flatpoly_verts`, `m_vert_count` to empty/zero and begins building the next frame immediately.

**Critical rule**: Every function that executes on the render thread (`GPURenderNow`, `GPURenderToFBO`, `gpu_execute_passes`) must read exclusively from `m_rt_*` members. Every function that executes on the game thread (`DrawIsometricView`, `append_triangle`, etc.) must write exclusively to the non-`m_rt_` members. Mixing these up produces a black world (reading an empty game-side buffer after FlipBuffers) or data races.

### 2B.1 DrawIsometricView — bucket walk (game thread)

`GLWorldViewRenderer::DrawIsometricView()` iterates `buckets[BUCKETS_COUNT-1]` down to `buckets[0]` (bucket 0 is included; GPU clips natively unlike the CPU rasterizer).

For each bucket, items are dispatched:

| Bucket kind | Action |
|---|---|
| `QK_PolygonStandard` / `QK_PolygonSimple` / `QK_PolygonNearFP` | `append_triangle()` — converts PolyPoint (fixed-point screen coords + UV + shade) to `WorldVertex` (float NDC + UV + shade) and appends to `m_verts` |
| `QK_TrigMode2/3/6` | `append_triangle_compact()` — same, from compact unsigned-short fields |
| `QK_PolyMode0/4/5` / `QK_BasicPolygon` | `m_flatpoly_verts.push_back()` — flat-colour vertex appended to flat poly list |
| `QK_JontySprite` / `QK_JontyISOSprite` | Calls `draw_jonty_mapwho()` → `draw_keepersprite()` → GPU intercept via `render_keepersprite_gpu()`, which batches the keeper sprite into the GPU sprite atlas and records a quad in `OpenGLSpriteBackend`. A `CMD_SPRITES` draw command is recorded. |
| `QK_CreatureShadow` | Shadow metadata is extracted (sprite index, angle, frame, 4× PolyPoint vertices, light darkness value) and stored as `ShadowCmd` in `m_shadow_cmds`. A `CMD_SHADOWS` draw command is recorded referencing `shadow_idx`. |
| `QK_CreatureStatus` / `QK_FloatingGoldText` / `QK_RoomFlagBottomPole` etc. | Handled by `draw_nonspatial_sprites_gpu()` → forwarded to `GLUIRenderer` (these render into the UI layer, not the world FBO). |
| `QK_SlabSelector` | Forwarded to `GLUIRenderer` as a line quad. |

When a `QK_JontySprite` is encountered, the accumulated tile vertices since the last flush are committed as a `CMD_TILES` draw command via `gpu_flush()`, which records the vertex range without issuing any GL calls. This maintains the correct interleaving: tiles occlude sprites at the same depth.

When a `QK_BasicPolygon` / flat-poly bucket entry is found, a `CMD_FLAT_POLYS` command is recorded referencing the current `m_flatpoly_verts` range.

### 2B.2 WorldVertex format

Each vertex in `m_verts` is a `WorldVertex`:

```cpp
struct WorldVertex {
    float x, y;      // NDC screen position: x in [-1,+1], y in [-1,+1]
    float z;         // NDC depth: -1 = near, +1 = far
    float u, v;      // Atlas UV (tile local [0,1], array layer encoded in tile_id)
    float shade;     // Per-vertex lighting (0 = dark, 1 = full brightness)
    float cam_z;     // Camera-space depth for perspective-correct UV interpolation
    int   tile_id;   // Flat index into tile atlas: variation * TEXTURE_BLOCKS_COUNT + local
};
```

Screen pixel coordinates are converted to NDC by dividing by `(m_screen_w/2, m_screen_h/2)` and shifting. Depth is mapped from bucket index: `z_ndc = 2 * bucket / (BUCKETS_COUNT - 1) - 1`.

### 2B.3 DrawCmd list

`m_draw_cmds` is a `std::vector<DrawCmd>`, each entry being one of:

| Type | Data | Meaning |
|---|---|---|
| `CMD_TILES` | `vert_start`, `vert_count` | Draw `vert_count` vertices from `m_rt_verts[vert_start]` with the tile shader |
| `CMD_SPRITES` | `bucket_num` | Flush all GPU sprite quads for this bucket (calls `OpenGLSpriteBackend::FlushForBucket()`) |
| `CMD_SHADOWS` | `shadow_idx`, `ndc_z` | Render one shadow silhouette from `m_rt_shadow_cmds[shadow_idx]` |
| `CMD_FLAT_POLYS` | `vert_start`, `vert_count` | Draw flat-colour geometry from `m_flatpoly_vbo` |

### 2B.4 gpu_execute_passes — GL render (render thread)

`gpu_execute_passes()` is called from `GPURenderNow()` (or `GPURenderToFBO()` for PiP). It reads exclusively from `m_rt_*`.

**Pass 1 — Opaque geometry (tiles + flat polys)**

Iterates `m_rt_draw_cmds` in order:

- `CMD_TILES`: one `glDrawArrays(GL_TRIANGLES, vert_start, vert_count)` using the world tile shader (palette-indexed texture array + per-pixel shade from fade LUT + per-subtile lightmap).
- `CMD_FLAT_POLYS`: uploads `m_rt_flatpoly_verts` to `m_flatpoly_vbo` on first occurrence, then `glDrawArrays` with the flat-poly shader. Writes depth, reads depth.
- `CMD_SPRITES` and `CMD_SHADOWS` are **skipped** in pass 1.

Depth test: `GL_LEQUAL`, depth write: on.

**Pass 2 — Shadows**

Iterates `m_rt_draw_cmds` again, processing only `CMD_SHADOWS`:

For each shadow:
1. `draw_keepsprite_unscaled_in_buffer()` decodes the RLE sprite silhouette into a 256×h CPU scratch buffer.
2. `glTexSubImage2D` uploads the grayscale silhouette to a reusable R8 texture.
3. Six float vertices covering the four world-space quad corners are built from `ShadowCmd::verts` (PolyPoint screen coords).
4. Shadow quad is drawn with `GL_BLEND = (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA)` — multiplies the frame dark value onto existing tile pixels. Depth test: `GL_LEQUAL`, depth write: off.

**Pass 3 — Sprites**

Iterates `m_rt_draw_cmds`, processing only `CMD_SPRITES`:

For each CMD_SPRITES bucket, `OpenGLSpriteBackend::Flush()` draws all keeper-sprite quads for that bucket. Depth test: `GL_LEQUAL`, depth write: off (sprites test against the tile depth buffer written in Pass 1 but do not modify it).

### 2B.5 Tile shader

`world.vert` / `world.frag`:

- **Vertex**: passes NDC position (x, y, z), atlas UV, shade, and tile layer index to the fragment stage.
- **Fragment**:
  - Samples the tile texture array (`sampler2DArray`, RGBA8).
  - Applies per-vertex shade (interpolated from `WorldVertex::shade`).
  - Looks up per-subtile dynamic lightmap (`usampler2D`, one `uint16` per subtile, uploaded every frame from `game.lish.subtile_lightness`).
  - Applies fog based on depth.
  - The palette is **not used** for tile geometry — tiles are stored as RGBA in the atlas.

### 2B.6 Keeper-sprite shader

`keepersprite.vert` / `keepersprite.frag`:

- Sprites are decoded from RLE into a per-sprite cache in a texture atlas (8-bit palette indices).
- Fragment shader samples the palette index texture, looks up the RGBA palette, applies shade remapping.

### 2B.7 Shadow shader

`shadow.vert` / `shadow.frag`:

- Shadow silhouette is stored as a single-channel R8 texture (white = opaque shadow, black = transparent).
- Fragment shader multiplies `darkness` by the R channel and outputs `(0, 0, 0, darkness * mask)` with `GL_BLEND = (GL_ZERO, ONE_MINUS_SRC_ALPHA)`.

### 2B.8 PiP (Picture-in-Picture)

`GPURenderToFBO()` renders the accumulated draw list into a caller-supplied FBO (used for the zoom box and the PiP scroll view). Because PiP renders are initiated by `draw_view()` on the render thread **after** `FlipBuffers()` has cleared the game-side buffers, PiP re-fills `m_draw_cmds` / `m_verts` / etc. directly via `DrawIsometricView()`. `GPURenderToFBO()` therefore reads from `m_rt_draw_cmds`, which at that point contains the PiP frame's data (not the main frame's, which was already cleared by `FlipBuffers()`).

---

## Summary: Data flow per frame

```
[Game thread]
  draw_view()
    ├─ camera matrix setup
    ├─ fiddle_gamut()           → minmaxs[]
    ├─ fill_in_points_*()       → back_ec[], front_ec[] (projected vertices)
    └─ do_a_plane_of_engine_columns_*()
         ├─ do_a_gpoly_*()      → buckets[] (BucketKindPolygonStandard etc.)
         └─ do_map_who()        → buckets[] (BucketKindJontySprite etc.)

  WorldViewRenderer_DrawIsometricView()
    └─ [SOFTWARE] display_drawlist()
         └─ draw_gpoly() / draw_keepersprite() → WScreen (8bpp CPU framebuffer)

    └─ [OPENGL]  GLWorldViewRenderer::DrawIsometricView()
         └─ bucket walk → m_verts[], m_draw_cmds[], m_shadow_cmds[], m_flatpoly_verts[]

  RendererOpenGL::EndFrame()
    ├─ FlipBuffers()           → move game buffers → m_rt_* (zero-copy)
    └─ signal render thread

[Render thread]
  GLWorldViewRenderer::GPURenderNow()
    └─ gpu_execute_passes()
         ├─ Pass 1: glDrawArrays (tiles/flatpoly) from m_rt_verts / m_rt_flatpoly_verts
         │    reads:  m_rt_draw_cmds, m_rt_vert_count, m_rt_verts, m_rt_flatpoly_verts
         ├─ Pass 2: shadow decode + blend from m_rt_shadow_cmds
         │    reads:  m_rt_draw_cmds, m_rt_shadow_cmds
         └─ Pass 3: keeper-sprite quads from OpenGLSpriteBackend
              reads:  m_rt_draw_cmds
```

---

## Common mistakes and invariants

| Rule | Consequence of violation |
|---|---|
| Every render-thread GL function (`gpu_execute_passes`, `GPURenderNow`, `GPURenderToFBO`) must read `m_rt_*` not `m_draw_cmds`/`m_shadow_cmds`/`m_flatpoly_verts` | Reads empty game-side buffers → black world (tiles/shadows/flatpolys are all absent) |
| Every game-thread scene-build function (`DrawIsometricView`, `append_triangle`, shadow recording) must write to game-side buffers only | Data races / frame tearing under Phase 3C |
| `FlipBuffers()` must be called while the render thread is idle | Concurrent read/write on `m_rt_*` → corruption |
| `GPURenderToFBO()` (PiP) must be called after `DrawIsometricView()` has filled the game-side buffers; `m_rt_*` at PiP time contains the PiP frame, not the main frame | Always true post-Phase-3B, but care is needed in Phase 3C |
| All three passes in `gpu_execute_passes` iterate `m_rt_draw_cmds` independently | Changing the member name in some passes but not others → missing geometry for those pass types |
