/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SpriteHandle.h
 *     Opaque handle for sprites registered with the renderer.
 * @par Purpose:
 *     The IUIRenderer virtual interface uses SpriteHandle instead of bflib
 *     TbSprite pointers, keeping the interface free of platform-specific types.
 *     The RendererManager C bridge is the only place that resolves TbSprite*
 *     to SpriteHandle before crossing the C/C++ boundary.
 *
 *     The value UINT32_MAX is reserved as the invalid/sentinel handle.
 */
/******************************************************************************/
#pragma once
#include <cstdint>

using SpriteHandle = uint32_t;
static constexpr SpriteHandle kInvalidSpriteHandle = UINT32_MAX;
static constexpr uint16_t     kSpriteHandleInvalidIndex = UINT16_MAX;

constexpr inline SpriteHandle MakeSpriteHandle(uint16_t generation, uint16_t index)
{
    return (SpriteHandle(generation) << 16) | SpriteHandle(index);
}

constexpr inline uint16_t SpriteHandleGeneration(SpriteHandle handle)
{
    return uint16_t(handle >> 16);
}

constexpr inline uint16_t SpriteHandleIndex(SpriteHandle handle)
{
    return uint16_t(handle & 0xFFFFu);
}
