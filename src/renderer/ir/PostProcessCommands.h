/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file PostProcessCommands.h
 *     IR commands for full-frame post-process effects.
 * @par Purpose:
 *     Post-process commands are submitted by the game thread (via IMapFadePass,
 *     ILensPass, etc.) and executed by the render thread after the main
 *     Shadow→World→UI→Text pipeline has completed.
 *
 *     Each effect is an optional<IREffectCmd> — at most one instance per frame.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <optional>
#include <utility>

/******************************************************************************/
// Map-fade transition
/******************************************************************************/

/** Composite command for the parchment ↔ 3D-view wipe transition.
 *  Written by IMapFadePass::FlushToRenderGraph() on the game thread.
 *  Consumed by IMapFadePass::ExecuteFromIR() on the render thread. */
struct IRMapFadeCmd
{
    float step            = 0.f;   ///< Interpolated wipe step [0.0..32.0].
    bool  capture_pending = false; ///< Render thread must re-capture both views before compositing.
};

/******************************************************************************/
// Combined post-process command buffers
/******************************************************************************/

struct PostProcessCommandBuffers
{
    std::optional<IRMapFadeCmd> map_fade;

    void Reset()
    {
        map_fade = std::nullopt;
    }

    void Swap(PostProcessCommandBuffers& other)
    {
        std::swap(map_fade, other.map_fade);
    }

    bool HasAnyCommands() const
    {
        return map_fade.has_value();
    }
};

/******************************************************************************/
