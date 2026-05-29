/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKMapFadePass.h
 *     Vulkan map-fade transition pass — delegates to software fallback.
 * @par Purpose:
 *     Phase-5b: wraps SoftwareMapFadePass so the parchment ↔ 3D view
 *     transition works correctly while the VK render-graph is being built.
 *     Replace with a native VK composite pass in Phase 7 once the render-
 *     graph is operational.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/backends/SoftwareMapFadePass.h"

/******************************************************************************/

class VKMapFadePass : public SoftwareMapFadePass {
public:
    const char* GetName() const override { return "VK_SW_FADE"; }
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
