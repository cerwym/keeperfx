#pragma once
/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererManager_Internal.h
 *     Internal helpers shared between RendererManager bridge files.
 *     NOT part of the public API — include only from RendererBridge_*.cpp.
 */
/******************************************************************************/
#include "renderer/SpriteHandle.h"

struct TbSprite;

/** Resolve a TbSprite pointer to its registered SpriteHandle.
 *  In GL mode: queries the sprite atlas.
 *  In software mode: queries the RendererManager-owned handle table. */
SpriteHandle RendererResolveSprite(const TbSprite* spr);
