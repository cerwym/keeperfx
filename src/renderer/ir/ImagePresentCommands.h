/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ImagePresentCommands.h
 *     IR command for presenting a full-screen, non-atlas image.
 * @par Purpose:
 *     Unifies the four ad-hoc GPU "blit" queues (raw opaque background,
 *     transparent overlay, landview zoom, FMV frame) into one IR command type
 *     carried in RenderGraph.  The game thread appends an IRImagePresentCmd per
 *     present; the render thread sorts by layer_z and dispatches on {format,kind}
 *     to the existing GL shaders.
 *
 * @par Format-agnostic by design (truecolour trail):
 *     `format` selects Indexed8 (today's palette-indexed path, wired) or RGBA8
 *     (truecolour — reserved; the render-thread dispatch carries an explicit
 *     "not implemented" stub).  This leaves the architecture ready for a future
 *     truecolour present + on-the-fly palette/colour-index toggle without a
 *     re-architecture.  See docs/fe-drawing-standardisation.md.
 *
 * @par Ownership:
 *     `pixels` and `embedded_palette` are owned; the buffers move with the
 *     command when RenderGraph swaps write<->read at Flip(), so the render
 *     thread reads a stable copy with no game-thread aliasing.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <vector>

/******************************************************************************/

/** Source pixel format of an image present. */
enum class PresentFormat : uint8_t {
    Indexed8 = 0,   ///< 8-bit palette indices, resolved via a palette LUT. Wired.
    RGBA8    = 1,   ///< Truecolour RGBA8. Reserved trail — dispatch stub only.
};

/** Where the palette comes from when format == Indexed8. */
enum class PresentPalette : uint8_t {
    Game     = 0,   ///< Live game palette (backgrounds, parchment, zoom).
    Embedded = 1,   ///< Per-present palette carried in embedded_palette (FMV, 256×BGRA).
    None     = 2,   ///< No palette (RGBA8).
};

/** Selects the GL execution path (shader / blend / uv-generation). */
enum class PresentKind : uint8_t {
    Opaque       = 0,   ///< Fills its rect; index 0 is a real colour. Rawblit shader.
    Transparent  = 1,   ///< Index-0 keyed transparent overlay. Staging blit + tint.
    LandviewZoom = 2,   ///< Fragment-shader zoom over the source. Zoom shader.
};

/** One full-screen image present. */
struct IRImagePresentCmd
{
    PresentFormat  format  = PresentFormat::Indexed8;
    PresentPalette palette = PresentPalette::Game;
    PresentKind    kind    = PresentKind::Opaque;

    int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    int src_w = 0, src_h = 0, src_pitch = 0;   ///< src_pitch used by Embedded/FMV; 0 ⇒ src_w.

    std::vector<uint8_t> pixels;            ///< owned: indices (Indexed8) or RGBA (RGBA8).
    std::vector<uint8_t> embedded_palette;  ///< owned: 256×4 BGRA when palette == Embedded.

    // LandviewZoom only:
    float zoom_center_map_x = 0.f, zoom_center_map_y = 0.f;
    float zoom_screen_cx    = 0.f, zoom_screen_cy    = 0.f;
    float zoom_scale        = 1.f;

    float layer_z = 0.f;   ///< Draw order; lower = earlier (backgrounds ≈ 0).
};

/** Per-frame image-present buffer (peer of UI/text buffers in RenderGraph). */
struct ImagePresentBuffers
{
    std::vector<IRImagePresentCmd> presents;

    void Reset()            { presents.clear(); }
    void Reserve(size_t n)  { presents.reserve(n); }
    void Swap(ImagePresentBuffers& o) { presents.swap(o.presents); }

    /** Append a default command; caller fills fields (and moves pixels in). */
    IRImagePresentCmd& AppendEmpty() { presents.emplace_back(); return presents.back(); }

    bool Empty() const { return presents.empty(); }
};

/******************************************************************************/
