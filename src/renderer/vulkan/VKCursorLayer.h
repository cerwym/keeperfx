/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKCursorLayer.h
 *     Vulkan cursor layer — delegates to software (CPU) fallback.
 * @par Purpose:
 *     Phase-5d: wraps SWCursorLayer so the OS pointer sprite and keeper-hand
 *     sprite render correctly while the VK UI pipeline is being built.
 *     Replace with a VK atlas-quad-based cursor in Phase 7 once
 *     VKUIRenderer is wired into the render-graph.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/backends/SWCursorLayer.h"

/******************************************************************************/

class VKCursorLayer : public SWCursorLayer {
public:
    const char* GetName() const override { return "VK_SW_CURSOR"; }
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
