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
#ifndef IPOST_PROCESS_PASS_H
#define IPOST_PROCESS_PASS_H

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
};

#endif // IPOST_PROCESS_PASS_H
