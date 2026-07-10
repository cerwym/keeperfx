/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file SpriteMaterialise.cpp
 *     Shared palette-index -> RGBA8 materialiser — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/SpriteMaterialise.hpp"
#include "post_inc.h"

/******************************************************************************/

std::vector<uint8_t> SpriteMaterialiseRGBA(
    const uint8_t* src, int w, int h, int src_stride,
    const unsigned char* palette, SpriteMaterialiseMode mode)
{
    std::vector<uint8_t> out;
    if (w <= 0 || h <= 0 || src == nullptr)
        return out;

    out.resize((size_t)w * (size_t)h * 4u);

    for (int y = 0; y < h; ++y) {
        const uint8_t* srow = src + (size_t)y * (size_t)src_stride;
        uint8_t*       drow = out.data() + (size_t)y * (size_t)w * 4u;
        for (int x = 0; x < w; ++x) {
            const uint8_t idx = srow[x];
            uint8_t* px = drow + (size_t)x * 4u;

            switch (mode) {
            case SpriteMaterialiseMode::RawIndex:
                // Structural view: show the index value as grayscale, fully opaque.
                px[0] = idx; px[1] = idx; px[2] = idx; px[3] = 255;
                break;

            case SpriteMaterialiseMode::AsDrawn:
            case SpriteMaterialiseMode::Truecolor:  // stub → AsDrawn until .fxspr exists
            default:
                if (idx == 0) {
                    // Palette index 0 is the engine's transparent colour.
                    px[0] = 0; px[1] = 0; px[2] = 0; px[3] = 0;
                } else if (palette != nullptr) {
                    // 6-bit VGA components -> 8-bit via <<2 (matches GL palette upload).
                    px[0] = (uint8_t)(palette[idx * 3 + 0] << 2);
                    px[1] = (uint8_t)(palette[idx * 3 + 1] << 2);
                    px[2] = (uint8_t)(palette[idx * 3 + 2] << 2);
                    px[3] = 255;
                } else {
                    // No palette available: fall back to grayscale so the shape is still visible.
                    px[0] = idx; px[1] = idx; px[2] = idx; px[3] = 255;
                }
                break;
            }
        }
    }
    return out;
}
