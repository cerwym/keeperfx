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
#include "player_data.h"

/******************************************************************************/

class VKMapFadePass : public SoftwareMapFadePass {
public:
    void PrepareBuffers(uint8_t* fade_src, uint8_t* fade_dest, int scanline, int height) override
    {
        (void)fade_src; (void)fade_dest; (void)scanline; (void)height;
    }

    int32_t StepFadeIn(int32_t step) override
    {
        (void)step;
        return (8 - get_my_player()->instance_remain_turns) * 4;
    }

    int32_t StepFadeOut(int32_t step) override
    {
        (void)step;
        return get_my_player()->instance_remain_turns * 4;
    }

    const char* GetName() const override { return "VK_SW_FADE"; }
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
