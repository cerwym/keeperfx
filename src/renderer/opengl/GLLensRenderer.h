/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLLensRenderer.h
 *     OpenGL implementation of ILensRenderer.
 * @par Purpose:
 *     Owns the *entire* GL realization of the lens system, so RendererOpenGL
 *     stays out of the lens business:
 *
 *       - the concrete GL lens passes (GLMistPass, ...) cached by effect type;
 *       - the offscreen compositing FBOs (scene + ping-pong pair) the world
 *         renders into when a geometric lens is active;
 *       - the passthrough blit shader that copies the final lens texture back to
 *         the default framebuffer;
 *       - the palette-lens UI-exclusion re-upload.
 *
 *     This is the ownership that previously lived partly in the game-side
 *     LensEffect (m_gpu_pass / CreateLensPass) and partly inline in
 *     RendererOpenGL::EndFrame_GL()/EnsureLensFBOs()/ApplyLensGPUPasses(); it now
 *     lives here as a first-class sub-renderer, peer to GLWorldViewRenderer /
 *     GLMapFadePass. RendererOpenGL::EndFrame_GL() drives it via three calls:
 *     BeginSceneCapture() -> (world render) -> ResolveAndApply() and, separately,
 *     ApplyPaletteUIExclusion().
 *
 *     The game side stays pure data: LensManager::FlushToRenderGraph() builds a
 *     pure-data IRLensCmd (effect type + params + palette + scope) which this
 *     class consumes on the render thread.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>

#include <vector>

#include "renderer/ILensRenderer.h"
#include "renderer/IPostProcessPass.h"   // LensGPUPassParams
#include "kfx/lense/LensEffect.h"        // LensEffectType

/******************************************************************************/

class RendererOpenGL;
struct IRLensCmd;
struct IRLensEffect;

class GLLensRenderer : public ILensRenderer {
public:
    explicit GLLensRenderer(RendererOpenGL* renderer);
    ~GLLensRenderer() override;

    const char* GetName() const override { return "GLLensRenderer"; }

    // ── Frame compositing (render thread; driven by EndFrame_GL) ─────────────

    /** Bind the lens scene FBO so world geometry (and swipe quads) render into it
     *  instead of the default framebuffer. Ensures/resizes the FBOs to w×h.
     *  Returns true when the scene FBO was bound — the caller must then call
     *  ResolveAndApply() after the world render. Returns false (leaving the
     *  default framebuffer bound) when there are no geometric passes this frame. */
    bool BeginSceneCapture(const IRLensCmd& cmd, int w, int h);

    /** Run the ping-pong GPU pass chain over the captured scene and blit the
     *  final result to the default framebuffer. Pairs with BeginSceneCapture(). */
    void ResolveAndApply(const IRLensCmd& cmd, int w, int h);

    /** Palette-lens UI exclusion: when a lens palette is active with WorldOnly
     *  scope, re-upload the shared palette texture with the BASE (non-lens)
     *  palette so subsequent UI/text/overhead draws decode without the lens tint.
     *  No-op otherwise (leaves the applied palette in place → legacy behaviour). */
    void ApplyPaletteUIExclusion(const IRLensCmd& cmd);

    /** Release all cached pass GPU resources and the compositing FBOs/shader.
     *  Called on backend teardown (GL context still current). Internal detail —
     *  no longer part of the ILensRenderer interface. */
    void ReleaseAll();

private:
    // One cache slot per LensEffectType value (only the GPU-capable kinds are
    // ever populated). Sized generously so static_cast<int>(type) always fits.
    static constexpr int kSlotCount = 8;

    struct Slot {
        IPostProcessPass* pass       = nullptr;
        LensGPUPassParams last_params;             // pointer fields always null (compare key)
        std::vector<unsigned char> last_mist;      // last configured mist payload
        std::vector<unsigned char> last_overlay;   // last configured overlay payload
        bool              configured = false;
    };

    static IPostProcessPass* CreatePass(LensEffectType type);

    /** Return a cached, configured pass for `effect` (created lazily, reconfigured
     *  only when its params or owned pixel payload change), or nullptr for non-GPU
     *  kinds. Resolves the effect's owned mist/overlay payload into the pass's
     *  configuration so no game-thread pointer is ever dereferenced here. Internal
     *  detail of the compositing chain — not part of the public ILensRenderer API. */
    IPostProcessPass* AcquireConfiguredPass(const IRLensEffect& effect);

    /** (Re-)create the scene + ping-pong FBOs and the passthrough shader for
     *  w×h. Idempotent when the size is unchanged and the scene FBO exists. */
    void EnsureFBOs(int w, int h);
    /** Free the compositing FBOs and passthrough shader (GL context must be current). */
    void DestroyFBOs();

    RendererOpenGL* m_renderer = nullptr;

    Slot m_slots[kSlotCount];

    // ── Compositing FBOs (owned) ─────────────────────────────────────────────
    // Scene FBO: world geometry renders here when a geometric lens is active.
    unsigned int m_lens_scene_fbo      = 0;
    unsigned int m_lens_scene_tex      = 0;  // GL_RGBA8 — decoded scene
    unsigned int m_lens_scene_depth_rb = 0;  // GL_DEPTH_COMPONENT24
    // Ping-pong pair for chaining multiple GPU lens passes.
    unsigned int m_lens_pass_fbo_a     = 0;
    unsigned int m_lens_pass_tex_a     = 0;
    unsigned int m_lens_pass_fbo_b     = 0;
    unsigned int m_lens_pass_tex_b     = 0;
    int          m_lens_fbo_w          = 0;
    int          m_lens_fbo_h          = 0;
    // Passthrough blit shader: copies final lens texture to default framebuffer.
    unsigned int m_passthrough_shader  = 0;
};

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
