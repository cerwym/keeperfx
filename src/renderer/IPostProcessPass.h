/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IPostProcessPass.h
 *     Interface for a single GPU post-processing pass.
 * @par Purpose:
 *     Defines the contract that all GPU lens-effect passes must satisfy.
 *     Each pass takes a source GL texture and writes to a destination FBO
 *     (or to the screen when dst_fbo == 0).
 */
/******************************************************************************/
#pragma once

#include <cstdint>

/**
 * Configuration parameters for a GPU lens pass, supplied once via
 * IPostProcessPass::Configure(), normally right after a successful Init().
 * Each concrete pass reads only the fields relevant to its own effect type.
 */
struct LensGPUPassParams
{
    // Mist
    const unsigned char* mist_data = nullptr;      // 256x256 mist amplitude texture
    int   mist_pos_x_step = 0, mist_pos_y_step = 0;
    int   mist_sec_x_step = 0, mist_sec_y_step = 0;
    // Current animation offsets (0..256) for the two drifting layers. The game side
    // owns the animation phase and advances it by game.delta_time each frame (so the
    // drift speed is frame-rate independent and identical to the software path); the
    // GPU pass consumes these absolute offsets rather than self-accumulating per frame.
    float mist_pos_x = 0.0f, mist_pos_y = 0.0f;
    float mist_sec_x = 50.0f, mist_sec_y = 128.0f;
    float mist_color[4]   = {0.5f, 0.5f, 0.5f, 0.3f}; // r,g,b,density
    // Fade amplitude table row base. The software renderer fades a pixel as
    // fade_data[(n<<8)+src] where fade_data = &pixmap.fade_tables[mist_lightness*256].
    // The accurate GL mist path reproduces this exactly by sampling the shared fade
    // table texture at row (mist_lightness + n); this carries mist_lightness across.
    int   mist_lightness  = 0;
    // Colour fidelity for the mist fade (OpenGL only; software is always paletted).
    // false = Accurate: reverse-map the scene to a palette index and run the exact
    //         8-bit DK fade table (bit-exact with software, banding included).
    // true  = Truecolor: smooth RGBA blend against the mist dirt colour.
    // Resolved on the game thread from the per-lens override or the global
    // g_renderer_settings.lens_color_mode default.
    bool  mist_truecolor  = false;

    // Geometric remap (Displacement + Flyeye).
    // The game side pre-computes the exact per-output-pixel source lookup table
    // (identical to the software path) and hands it over as a tightly-packed
    // RG16 image: two uint16 values per pixel = (src_x, src_y) in pixel coords,
    // row-major, top row first. The backend uploads it to a GL_RG16UI texture and
    // samples the scene through it, so GL is pixel-identical to software instead
    // of a procedural lookalike. remap_data is re-pointed at the owning IR vector
    // by the backend just before Configure(); the IR itself never carries a live
    // game-thread pointer (see IRLensEffect). remap_version bumps whenever the
    // table is rebuilt (lens or resolution change) and gates re-upload.
    const unsigned char* remap_data    = nullptr;
    int                  remap_w        = 0;
    int                  remap_h        = 0;
    uint32_t             remap_version  = 0;

    // Overlay
    const unsigned char* overlay_data = nullptr;
    int   overlay_w = 0, overlay_h = 0;
    float overlay_alpha = 1.0f;
};

/**
 * A single GPU post-processing pass.
 *
 * This interface is platform-neutral by design.  GL handle types are
 * represented as unsigned int (identical in size/value to GLuint on every
 * platform that uses OpenGL/GLES) so that no GL header is needed here.
 * Platform-specific implementations cast to the appropriate type internally.
 *
 * Life cycle:
 *   Init()   — allocate GPU resources (called when the owning ILensRenderer
 *              first acquires the pass)
 *   Apply()  — execute the shader pass (called every frame by the GPU renderer)
 *   Free()   — release GPU resources (called when the ILensRenderer releases)
 *
 * Concrete passes are owned and cached by a per-backend ILensRenderer, so this
 * interface is never instantiated on platforms without a GPU lens renderer.
 */
class IPostProcessPass {
public:
    virtual ~IPostProcessPass() = default;

    /**
     * Allocate GPU resources for this pass.
     * @return true on success.
     */
    virtual bool Init() = 0;

    /**
     * Execute the pass.
     * @param src_tex   GPU texture handle containing the source image (RGBA).
     * @param dst_fbo   Framebuffer object to render into.  Pass 0 for the screen.
     * @param src_w     Width of src_tex in pixels.
     * @param src_h     Height of src_tex in pixels.
     */
    virtual void Apply(unsigned int src_tex, unsigned int dst_fbo, int src_w, int src_h) = 0;

    /** Release all GPU resources. Safe to call on a not-yet-initialised pass. */
    virtual void Free() = 0;

    /** Apply effect-specific configuration. Called once by the owning
     *  LensEffect::Setup() on the game thread, after a successful Init().
     *  Default: no-op. */
    virtual void Configure(const LensGPUPassParams& /*params*/) {}
};
