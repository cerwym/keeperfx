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

#include <array>
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
// Lens post-process passes
/******************************************************************************/

/** Bound at 4 = number of GPU-capable lens effect types today
 *  (Mist, Displacement, Flyeye, Overlay). Palette has no GPU pass. */
inline constexpr int kMaxLensGPUPasses = 4;

/** 0..N active GPU lens passes for this frame, in LensManager registration
 *  order. Written by LensManager::FlushToRenderGraph() on the game thread.
 *  Consumed by the GL/Vita backend's frame compositing (ping-pong FBO
 *  chain), analogous to IMapFadePass::ExecuteFromIR(). */
struct IRLensCmd
{
    std::array<class IPostProcessPass*, kMaxLensGPUPasses> passes{};
    int count = 0;
};

/******************************************************************************/
// Combined post-process command buffers
/******************************************************************************/

struct PostProcessCommandBuffers
{
    std::optional<IRMapFadeCmd> map_fade;
    std::optional<IRLensCmd>    lens;

    void Reset()
    {
        map_fade = std::nullopt;
        lens = std::nullopt;
    }

    void Swap(PostProcessCommandBuffers& other)
    {
        std::swap(map_fade, other.map_fade);
        std::swap(lens, other.lens);
    }

    bool HasAnyCommands() const
    {
        return map_fade.has_value() || lens.has_value();
    }
};

/******************************************************************************/
