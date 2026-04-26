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
#ifndef GL_MAP_FADE_PASS_H
#define GL_MAP_FADE_PASS_H

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include "renderer/IMapFadePass.h"

/******************************************************************************/

class GLMapFadePass : public IMapFadePass {
public:
    GLMapFadePass();
    ~GLMapFadePass() override;

    // ── IMapFadePass ─────────────────────────────────────────────────────────

    /** Render one fade-in frame (parchment → 3D world).
     *  Captures both views on step == 0.  Records the current step so
     *  RenderGPUComposePass() can draw the wipe at EndFrame() time.
     *  Falls back to map_fade_in() if Init() or capture failed. */
    int32_t StepFadeIn(int32_t step) override;

    /** Render one fade-out frame (3D world → parchment).
     *  Captures both views on step == 32.  Same GPU/fallback logic as above. */
    int32_t StepFadeOut(int32_t step) override;

    const char* GetName() const override { return "OPENGL"; }

    // ── GPU compose hook ─────────────────────────────────────────────────────

    /** Returns true while a transition is active and frames were captured. */
    bool HasGPUComposePass() const override { return m_active; }

    bool SupportsNativeResolution() const override { return m_initialized; }

    /** Renders the wipe quad using the step stored by the last Step* call.
     *  Called by RendererOpenGL::EndFrame() after the staging palette blit. */
    void RenderGPUComposePass() override;

private:
    bool Init();
    void Shutdown();
    bool CaptureAndUploadFrames();
    void MarkDone();

    // GL resources
    GLuint m_tex[2]      = {};  // [0]=parchment, [1]=3D world — GL_RGBA8 native res
    GLuint m_vao         = 0;
    GLuint m_vbo         = 0;
    GLuint m_prog        = 0;

    GLint  m_loc_step      = -1;
    GLint  m_loc_parchment = -1;
    GLint  m_loc_world     = -1;

    bool m_initialized = false;
    bool m_active      = false; ///< true while a transition is in progress
    bool m_deactivate_after_render = false; ///< deactivate after next compose pass
    long m_step        = 0;     ///< step recorded for RenderGPUComposePass()
    int  m_tex_w       = 0;
    int  m_tex_h       = 0;
};

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
#endif // GL_MAP_FADE_PASS_H
