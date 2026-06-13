/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKTextRenderer.h
 *     Vulkan text renderer — delegates to software fallback.
 * @par Purpose:
 *     Phase-5c: wraps SoftwareTextRenderer so all font / text-window / draw
 *     paths work correctly while the VK IR text pipeline is being built.
 *     Replace with a native VK glyph-quad renderer in Phase 7 once the
 *     VK UIRenderer is operational (text quads can be batched into the UI
 *     command stream alongside sprite quads).
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/backends/SoftwareTextRenderer.h"

/******************************************************************************/

class VKTextRenderer : public SoftwareTextRenderer {
public:
    const char* GetName() const override { return "VK_SW_TEXT"; }
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
