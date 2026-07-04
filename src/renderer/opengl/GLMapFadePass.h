/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLMapFadePass.h
 *     Desktop OpenGL GPU implementation of the map-fade transition.
 * @par Purpose:
 *     Implements IMapFadePass for the desktop OpenGL path.
 *
 *     On the first call to StepFadeIn(0) / StepFadeOut(32) it renders
 *     both the 3D-world view and the parchment overhead view into
 *     offscreen FBOs, producing two GL_RGBA8 textures at native
 *     window resolution.
 *
 *     Every subsequent step call records the current step value; the wipe
 *     quad itself is not drawn here — instead HasGPUComposePass() returns
 *     true, and RendererOpenGL::EndFrame() calls RenderGPUComposePass() to
 *     draw a fullscreen quad using MAP_FADE_FRAG_SHADER.
 *
 *     The fragment shader is a faithful GLSL port of map_fade() in
 *     engine_redraw.c: UV-warp + per-image fade factor + additive RGB blend,
 *     running at native display resolution instead of 320×200.
 *
 * @par CPU→GPU upgrade from SoftwareMapFadePass:
 *     Software path writes palette-indexed pixels to WScreen (320×200).
 *     This path writes nothing to WScreen — the staging blit is skipped when
 *     world_gpu_active is true — and instead composites at native resolution
 *     after the regular EndFrame pipeline.
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include "renderer/IMapFadePass.h"
#include "renderer/opengl/IGLShaderCompilable.h"

/******************************************************************************/

class RendererOpenGL;

class GLMapFadePass : public IMapFadePass, public IGLShaderCompilable {
public:
    explicit GLMapFadePass(RendererOpenGL* renderer);
    ~GLMapFadePass() override;

    // ── IMapFadePass ─────────────────────────────────────────────────────────

    void PrepareBuffers(uint8_t* fade_src, uint8_t* fade_dest, int scanline, int height) override
    {
        (void)fade_src; (void)fade_dest; (void)scanline; (void)height;
    }

    /** Render one fade-in frame (parchment → 3D world).
     *  Captures both views on step == 0.  Records the current step and sets
     *  m_capture_pending so FlushToRenderGraph() can emit the IR command.
     *  Falls back to map_fade_in() if Init() failed. */
    int32_t StepFadeIn(int32_t step) override;

    /** Render one fade-out frame (3D world → parchment).
     *  Captures both views on step == 32.  Same logic as StepFadeIn above. */
    int32_t StepFadeOut(int32_t step) override;

    const char* GetName() const override { return "OPENGL"; }
    const char* RendererName() const override { return "GLMapFadePass"; }

    // ── IGLShaderCompilable ───────────────────────────────────────────────────

    /** Compile the map-fade shader and initialise all GL resources.
     *  Idempotent — returns true immediately if already initialised.
     *  Called by the bootstrapper in RendererManager::RendererInit(). */
    bool CompileShaders() override;

    // ── IR interface ─────────────────────────────────────────────────────────

    /** Push an IRMapFadeCmd into the graph's write-side post-process buffers if
     *  a transition is active.  Handles the terminal-frame snap when view_mode
     *  has left ParchFade mode.  Called by RendererOpenGL::EndFrame() on the
     *  game thread before Flip(). */
    void FlushToRenderGraph(RenderGraph& graph) override;

    /** Execute the map-fade wipe composite on the render thread.
     *  Captures the parchment view if cmd.capture_pending (and flags the
     *  world-view capture as pending — see CaptureWorldFrameIfPending()),
     *  then draws the wipe quad using the previously-captured world view.
     *  Called by RendererOpenGL::EndFrame_GL() after the staging palette blit. */
    void ExecuteFromIR(const IRMapFadeCmd& cmd) override;

    /** Blits the default framebuffer (world geometry + GameUI, both already
     *  drawn this frame) into the world-view snapshot texture, if
     *  ExecuteFromIR() flagged a capture as pending this frame. Called by
     *  RendererOpenGL::EndFrame_GL() after DrawGameUI(). */
    void CaptureWorldFrameIfPending() override;

    bool SupportsNativeResolution() const override { return m_initialized; }

    /** Notify of current OS-window dimensions so CaptureParchmentFrame()/
     *  CaptureWorldFrame() do not need to read RendererGetScreenWidth()/Height directly.
     *  Called by RendererOpenGL::BeginFrame(). */
    void SetScreenSize(int w, int h) override { m_screen_w = w; m_screen_h = h; }

private:
    void Shutdown();
    /** Resizes both snapshot textures and captures the parchment overhead
     *  view into m_tex[0] (FlushSceneToFBO). Does NOT touch m_tex[1] — see
     *  CaptureWorldFrameIfPending(). */
    bool CaptureParchmentFrame();
    /** Blits the current default framebuffer into m_tex[1] (the world-view
     *  snapshot). Split out from the parchment capture so it can run after
     *  DrawGameUI() has drawn for the frame — see CaptureWorldFrameIfPending(). */
    bool CaptureWorldFrame();

    // GL resources
    GLuint m_tex[2]      = {};  // [0]=parchment, [1]=3D world — GL_RGBA8 native res
    GLuint m_vao         = 0;
    GLuint m_vbo         = 0;
    GLuint m_prog        = 0;

    GLint  m_loc_step      = -1;
    GLint  m_loc_parchment = -1;
    GLint  m_loc_world     = -1;

    bool m_initialized = false;

    // Game-thread-only state — written by StepFadeIn/Out, read by FlushToRenderGraph().
    // Must not be accessed from the render thread.
    bool  m_active          = false; ///< true while a transition is in progress
    bool  m_capture_pending = false; ///< frame capture deferred to render thread
    float m_step            = 0.f;   ///< current interpolated step value
    int   m_tex_w           = 0;
    int   m_tex_h           = 0;

    // Render-thread-only state — set by ExecuteFromIR(), consumed and
    // cleared by CaptureWorldFrameIfPending() later the same frame.
    bool  m_world_capture_pending = false;

    // Full OS-window dimensions — set by SetScreenSize(), eliminates RendererGetScreenWidth()/Height reads.
    int  m_screen_w    = 0;
    int  m_screen_h    = 0;

    RendererOpenGL* m_renderer = nullptr;
};

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
