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
#include <vector>

#include "renderer/IPostProcessPass.h"   // LensGPUPassParams (pure data)

// Forward-declared with a fixed underlying type (opaque enum), so it is a
// complete type usable by value here without pulling in the kfx/lense header.
enum class LensEffectType;

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

/** UI scope for the palette lens.
 *  - FullFrame: palette recolors world AND UI/text (legacy / accurate).
 *  - WorldOnly: palette recolors world only; UI/text keep the base palette. */
enum class LensScope : uint8_t { FullFrame = 0, WorldOnly = 1 };

/** One GPU-capable lens effect as pure, self-contained data: which effect + its
 *  parameters + any pixel payload the effect needs, copied BY VALUE.
 */
struct IRLensEffect
{
    LensEffectType    type{};   ///< GPU-capable effect kind (opaque enum, value only).
    LensGPUPassParams params{}; ///< Pure-data effect parameters (pointer fields always null).

    /// Owned mist amplitude texture (256x256, single channel). Empty unless this is a mist effect.
    std::vector<unsigned char> mist_pixels;
    /// Owned overlay image (overlay_w * overlay_h * 4, RGBA). Empty unless this is an overlay effect.
    std::vector<unsigned char> overlay_pixels;
    /// Owned geometric remap table (remap_w * remap_h * 4 bytes = two uint16 per
    /// pixel: src_x, src_y). Only populated on the frame the table changes
    /// (params.remap_version bumps); empty on unchanged frames — the backend keeps
    /// the previously-uploaded texture keyed by remap_version. Empty for non-remap
    /// effects (Mist / Overlay / Palette).
    std::vector<unsigned char> remap_pixels;
};
struct IRLensCmd
{
    std::array<IRLensEffect, kMaxLensGPUPasses> effects{};
    int count = 0;

    // Palette lens side-channel (pure data). `has_palette` means a lens palette
    // is active this frame
    bool                    has_palette   = false;
    LensScope               palette_scope = LensScope::FullFrame;
    std::array<uint8_t, 768> palette{};   ///< Base (non-lens) UI palette, 6-bit RGB values.
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
