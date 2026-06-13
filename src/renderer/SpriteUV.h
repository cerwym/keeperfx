/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SpriteUV.h
 *     Shared UV coordinate type for sprite atlases.
 * @par Purpose:
 *     Shared between GLSpriteAtlas and VKSpriteAtlas so that callers
 *     can use SpriteUV without depending on a specific renderer backend.
 */
/******************************************************************************/
#pragma once

#include <cstdint>

struct SpriteUV {
    float u0, v0, u1, v1;
    uint16_t pixel_w, pixel_h;  // original sprite pixel dimensions
};
