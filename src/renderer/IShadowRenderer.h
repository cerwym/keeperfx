/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IShadowRenderer.h
 *     Abstract interface for dedicated shadow rendering backends.
 */
/******************************************************************************/
#pragma once

#include "renderer/ir/ShadowCommands.h"

/******************************************************************************/

class IShadowRenderer {
public:
    virtual ~IShadowRenderer() = default;
    virtual void Execute(const ShadowCommandBuffers& cmds) = 0;
};

/******************************************************************************/
