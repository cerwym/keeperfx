/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IPostProcessPass.h
 *     Interface for a single GPU post-processing pass.
 * @par Purpose:
 *     Defines the contract that all GPU lens-effect passes must satisfy.
 *     Each pass takes a source GL texture and writes to a destination FBO
 *     (or to the screen when dst_fbo == 0).
 *
 *     Desktop/software renderers return nullptr from LensEffect::GetGPUPass(),
 *     so this interface is never instantiated on non-Vita platforms.
 */
/******************************************************************************/
#pragma once

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
    float mist_color[4]   = {0.5f, 0.5f, 0.5f, 0.3f}; // r,g,b,density

    // Displacement — raw config values (unscaled); each concrete pass applies
    // its own platform-specific scaling, matching pre-refactor per-backend behavior.
    int   displace_algorithm = 0;
    float displace_magnitude = 0.0f;
    float displace_period    = 0.0f;

    // Flyeye
    float flyeye_hex_size = 0.03f;

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
 *   Init()   — allocate GPU resources (called when the parent LensEffect activates)
 *   Apply()  — execute the shader pass (called every frame by the GPU renderer)
 *   Free()   — release GPU resources (called when the parent LensEffect deactivates)
 *
 * Renderers that do not support GPU passes return false from
 * IRenderer::SupportsGPUPasses(), and LensEffect::GetGPUPass() returns nullptr
 * on all effects by default, so this interface is never instantiated on
 * platforms without a GPU renderer.
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
