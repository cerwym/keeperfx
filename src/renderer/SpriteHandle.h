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
