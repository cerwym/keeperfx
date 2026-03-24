/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file WorldVertex.h
 *     GPU vertex format for world geometry (isometric map polygons).
 * @par Purpose:
 *     Defines the WorldVertex struct used by GLWorldViewRenderer to feed the
 *     world geometry pipeline.  Also provides the POLYPOINT_TO_WORLDVERTEX
 *     macro that converts a PolyPoint (fixed-point 16:16) to float NDC coords.
 *
 *     The layout maps directly to the vertex shader attributes:
 *       location 0: a_pos   (x, y) — NDC clip space [-1, 1]
 *       location 1: a_uv    (u, v) — texture coords  [0, 1]
 *       location 2: a_shade (s)    — lighting         [0, 1]
 *
 *     Fixed-point conventions (from bflib_render.h / engine_render.c):
 *       X, Y  — 16:16 fixed point, integer part is screen pixel coordinate
 *       U, V  — 16:16 fixed point, integer part is texel index (0..255)
 *       S     — shade level; high byte (>> 8) is palette shade index (0..255)
 */
/******************************************************************************/
#ifndef WORLD_VERTEX_H
#define WORLD_VERTEX_H

#include <stddef.h>  /* offsetof */

/******************************************************************************/

/** One vertex in the GPU world geometry pipeline. */
struct WorldVertex {
    float x;      /**< NDC horizontal: [-1, 1], left = -1 */
    float y;      /**< NDC vertical:   [-1, 1], bottom = -1 */
    float u;      /**< Texture U:  [0, 1] */
    float v;      /**< Texture V:  [0, 1] */
    float shade;  /**< Lighting:   [0, 1], 0 = dark, 1 = full-bright */
};

/******************************************************************************/

/**
 * Convert a PolyPoint field from fixed-point 16:16 to a float screen pixel.
 * The integer part is extracted with >> 16.
 */
#define FP16_TO_FLOAT(fp)   ((float)((fp) >> 16))

/**
 * Convert a PolyPoint screen X (fixed 16:16) to NDC horizontal.
 * @param fp        PolyPoint::X value
 * @param screen_w  Render target width in pixels
 */
#define FP_X_TO_NDC(fp, screen_w) \
    (FP16_TO_FLOAT(fp) / (float)(screen_w) * 2.0f - 1.0f)

/**
 * Convert a PolyPoint screen Y (fixed 16:16) to NDC vertical.
 * Screen Y increases downward; NDC Y increases upward — flip here.
 * @param fp        PolyPoint::Y value
 * @param screen_h  Render target height in pixels
 */
#define FP_Y_TO_NDC(fp, screen_h) \
    (1.0f - FP16_TO_FLOAT(fp) / (float)(screen_h) * 2.0f)

/**
 * Convert a PolyPoint texture U (fixed 16:16) to normalised [0,1].
 * Tile textures are 256 wide (k_tile_dim=32 packed 8-wide = 256-texel range).
 */
#define FP_U_TO_UV(fp)   (FP16_TO_FLOAT(fp) / 256.0f)

/** Convert a PolyPoint texture V (fixed 16:16) to normalised [0,1]. */
#define FP_V_TO_UV(fp)   (FP16_TO_FLOAT(fp) / 256.0f)

/**
 * Convert a PolyPoint shade S (fixed 16:16, high byte = shade index 0–255)
 * to a normalised float shade value.
 */
#define FP_S_TO_SHADE(fp)  ((float)(((fp) >> 8) & 0xFF) / 255.0f)

/**
 * Fill a WorldVertex from a PolyPoint struct ptr.
 * @param wv        Pointer to destination WorldVertex.
 * @param pp        Pointer to source PolyPoint.
 * @param sw        Screen width (pixels).
 * @param sh        Screen height (pixels).
 */
#define POLYPOINT_TO_WORLDVERTEX(wv, pp, sw, sh) \
    do { \
        (wv)->x     = FP_X_TO_NDC((pp)->X, (sw)); \
        (wv)->y     = FP_Y_TO_NDC((pp)->Y, (sh)); \
        (wv)->u     = FP_U_TO_UV((pp)->U); \
        (wv)->v     = FP_V_TO_UV((pp)->V); \
        (wv)->shade = FP_S_TO_SHADE((pp)->S); \
    } while (0)

/**
 * Fill a WorldVertex from compact-format bucket data.
 * Compact types store screen X/Y as unsigned short (raw pixel coord, NOT
 * fixed-point 16:16) and texture U/V as unsigned char (0..255 texel index).
 * @param wv     Pointer to destination WorldVertex.
 * @param sx     Screen X (unsigned short, raw pixel coord).
 * @param sy     Screen Y (unsigned short, raw pixel coord).
 * @param u8     Texture U (unsigned char, 0..255 texel index).
 * @param v8     Texture V (unsigned char, 0..255 texel index).
 * @param shade8 Shade index (unsigned char, 0..255); pass 255 for full-bright.
 * @param sw     Screen width (pixels).
 * @param sh     Screen height (pixels).
 */
#define COMPACT_UV_TO_WORLDVERTEX(wv, sx, sy, u8, v8, shade8, sw, sh) \
    do { \
        (wv)->x     = ((float)(sx) / (float)(sw)) * 2.0f - 1.0f; \
        (wv)->y     = 1.0f - ((float)(sy) / (float)(sh)) * 2.0f; \
        (wv)->u     = (float)(u8) / 256.0f; \
        (wv)->v     = (float)(v8) / 256.0f; \
        (wv)->shade = (float)(shade8) / 255.0f; \
    } while (0)

/******************************************************************************/

#endif // WORLD_VERTEX_H
