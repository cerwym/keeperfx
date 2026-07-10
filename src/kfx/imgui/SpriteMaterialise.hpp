/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file SpriteMaterialise.hpp
 *     Shared palette-index -> RGBA8 materialiser for sprite debug tooling.
 *
 * @par Purpose:
 *     Single source of the "how does a palette-indexed sprite become RGBA"
 *     rule, so the debug atlas viewer and the future truecolor asset pipeline
 *     agree pixel-for-pixel.  The engine stores sprites as 6-bit VGA
 *     palette indices (index 0 == transparent); this converts a rectangular
 *     region of those indices into tightly-packed RGBA8.
 *
 *     This is deliberately backend-agnostic (no GL / no ImGui) so it can be
 *     reused by pngpal2raw-side parity checks and runtime materialisation.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

/** How a palette index is turned into an RGBA texel. */
enum class SpriteMaterialiseMode {
    AsDrawn,    ///< Palette colour; index 0 -> fully transparent. What the GL renderer shows.
    RawIndex,   ///< Grayscale = raw index value, fully opaque. Structural/debug view.
    Truecolor,  ///< Stub: identical to AsDrawn until a canonical-RGBA (.fxspr) source exists.
};

/** Materialise a w*h block of palette indices into RGBA8.
 *
 *  @param src        Pointer to the top-left source index.
 *  @param w,h        Region size in pixels.
 *  @param src_stride Source row stride in bytes (>= w).
 *  @param palette    768-byte 6-bit VGA palette (R,G,B per index); scaled <<2 to 8-bit.
 *                    May be null, in which case AsDrawn/Truecolor fall back to grayscale.
 *  @param mode       Conversion rule (see SpriteMaterialiseMode).
 *  @return Tightly-packed RGBA8 buffer of exactly w*h*4 bytes (empty if w<=0 || h<=0).
 */
std::vector<uint8_t> SpriteMaterialiseRGBA(
    const uint8_t* src, int w, int h, int src_stride,
    const unsigned char* palette, SpriteMaterialiseMode mode);
