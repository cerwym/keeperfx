/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLMapFadePass.cpp
 *     Desktop OpenGL GPU implementation of the map-fade transition.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/engine/cameras.h"
#include "renderer/opengl/GLMapFadePass.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLShaders.h"
#include "renderer/RendererOpenGL.h"      // FlushSceneToFBO
#include "renderer/RendererManager.h"     // RendererGetActive, WorldViewRenderer_BeginWorldPass
#include "renderer/opengl/GLWorldViewRenderer.h" // BeginWorldPass
#include "engine_redraw.h"    // redraw_isometric_view, redraw_frontview, map_fade_in/out, setup/store_engine_window
#include "engine_render.h"    // EngineRenderState, engine_save/restore_render_state, draw_view
#include "gui_parchment.h"    // load_parchment_file, redraw_minimal_overhead_view
#include "player_data.h"      // get_my_player, view_mode_restore
#include "game_legacy.h"      // game.process_turn_time
#include "config_keeperfx.h"   // is_feature_on, Ft_DeltaTime
#include "local_camera.h"     // get_local_camera

#include <vector>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

// Fullscreen quad — position only (xy), 6 vertices.
static const float k_quad_verts[] = {
    -1.f, -1.f,
     1.f, -1.f,
     1.f,  1.f,
    -1.f, -1.f,
     1.f,  1.f,
    -1.f,  1.f,
};

/******************************************************************************/

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ERRORLOG("GLMapFadePass shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/

GLMapFadePass::GLMapFadePass()
{
    // GL resources are initialised by CompileShaders(),
    // called by the bootstrapper in RendererManager::RendererInit().
}

GLMapFadePass::~GLMapFadePass()
{
    Shutdown();
}

bool GLMapFadePass::CompileShaders()
{
    if (m_initialized)
        return true;

    // ── Compile shader ────────────────────────────────────────────────────────
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   MAP_FADE_VERT_SHADER);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, MAP_FADE_FRAG_SHADER);
    if (!vert || !frag)
    {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        WARNLOG("GLMapFadePass: shader compilation failed; falling back to software");
        return false;
    }
    m_prog = glCreateProgram();
    glAttachShader(m_prog, vert);
    glAttachShader(m_prog, frag);
    glLinkProgram(m_prog);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(m_prog, sizeof(log), nullptr, log);
        ERRORLOG("GLMapFadePass program link error: %s", log);
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }

    // Cache uniform locations and set sampler bindings once.
    m_loc_step      = glGetUniformLocation(m_prog, "u_step");
    m_loc_parchment = glGetUniformLocation(m_prog, "u_parchment");
    m_loc_world     = glGetUniformLocation(m_prog, "u_world");
    glUseProgram(m_prog);
    glUniform1i(m_loc_parchment, 0);  // GL_TEXTURE0
    glUniform1i(m_loc_world,     1);  // GL_TEXTURE1
    glUseProgram(0);

    // ── Fullscreen quad VAO ───────────────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_quad_verts), k_quad_verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // ── Snapshot textures ─────────────────────────────────────────────────────
    glGenTextures(2, m_tex);
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, m_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    m_initialized = true;
    return true;
}

void GLMapFadePass::Shutdown()
{
    if (m_prog)  { glDeleteProgram(m_prog);          m_prog = 0; }
    if (m_vao)   { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)   { glDeleteBuffers(1, &m_vbo);       m_vbo = 0; }
    if (m_tex[0]) { glDeleteTextures(2, m_tex); m_tex[0] = m_tex[1] = 0; }
    m_initialized = false;
    m_active      = false;
}

/******************************************************************************/

bool GLMapFadePass::CaptureAndUploadFrames()
{
    RendererOpenGL* gl_rend = static_cast<RendererOpenGL*>(RendererGetActive());
    if (!gl_rend)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — no active renderer");
        return false;
    }

    struct PlayerInfo* player = get_my_player();
    const int w = m_screen_w;
    const int h = m_screen_h;
    if (w < 1 || h < 1)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — degenerate screen size %dx%d", w, h);
        return false;
    }

    m_tex_w = w;
    m_tex_h = h;

    // ── Create temporary FBO + depth renderbuffer ────────────────────────
    GLuint fbo = 0, depth_rb = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Resize both snapshot textures to the window resolution.
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, m_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Capture 3D world view → m_tex[1] ────────────────────────────────
    // Follow the PiP pattern: save engine state, set up engine window for
    // the FBO dimensions, call BeginWorldPass, render, flush to FBO, restore.
    // The GL renderer uses a fullscreen viewport (0,0,w,h) — the sidebar
    // paints on top — so the captured view always matches the live view.
    {
        // Save engine projection/window state and set up for FBO dimensions.
        struct EngineRenderState saved_state;
        engine_save_render_state(&saved_state);

        setup_engine_window(0, 0, w, h);

        // Tell the world renderer about the FBO target dimensions.
        // BeginWorldPass also sets vec_window_width/height from w,h.
        gl_rend->GetWorldRenderer()->BeginWorldPass(nullptr, 0, w, h, 0, 0);

        // Re-render the 3D view using the appropriate camera.
        struct Camera* cam;
        if (player->view_mode_restore == PVM_IsoWibbleView ||
            player->view_mode_restore == PVM_IsoStraightView)
            cam = get_local_camera(CamIV_Isometric);
        else
            cam = get_local_camera(CamIV_FrontView);
        draw_view(cam, 0);

        // Restore engine projection/window state.
        engine_restore_render_state(&saved_state);

        // Flush world geometry into the FBO.
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tex[1], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_rb);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl_rend->FlushSceneToFBO(w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Capture parchment overhead view → m_tex[0] ──────────────────────
    {
        // Queue parchment background + overhead map via the normal game path.
        load_parchment_file();
        redraw_minimal_overhead_view();

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tex[0], 0);
        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl_rend->FlushSceneToFBO(w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    glDeleteRenderbuffers(1, &depth_rb);
    glDeleteFramebuffers(1, &fbo);
    glViewport(0, 0, w, h);

    return true;
}

void GLMapFadePass::MarkDone()
{
    m_active          = false;
    m_step            = 0.f;
    m_capture_pending = false;
}

/******************************************************************************/

int32_t GLMapFadePass::StepFadeIn(int32_t step)
{
    if (!m_initialized)
    {
        WARNLOG("GLMapFadePass: GPU init failed, skipping fade");
        int32_t next = step + 4;
        return (next > 32) ? 32 : next;
    }

    // Capture on the first call (step == 0).
    // Deferred to the render thread: CaptureAndUploadFrames() makes GL calls and
    // must not run on the game thread (GL context lives on the render thread after
    // the first EndFrame).  Set m_capture_pending here; RenderGPUComposePass()
    // will perform the actual capture when called from EndFrame_GL().
    if (step == 0)
    {
        m_deactivate_after_render = false;
        m_capture_pending = true;
        m_active = true;
    }

    // Derive step from instance_remain_turns and interpolate between game
    // ticks using process_turn_time so the fade runs at display refresh rate
    // instead of the 20 fps game-tick rate.
    //   Base formula: (8 - remain) * 4   →  4 at remain=7 … 28 at remain=1
    //   Interpolated: (8 - remain + frac) * 4  where frac ∈ [0,1)
    float remain = (float)get_my_player()->instance_remain_turns;
    float frac = (is_feature_on(Ft_DeltaTime) && remain > 0.f)
               ? (float)game.process_turn_time : 0.f;
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    float derived = (8.f - remain + frac) * 4.f;
    if (derived < 0.f) derived = 0.f;
    if (derived > 32.f) derived = 32.f;
    m_step = derived;

    return (int32_t)derived;
}

int32_t GLMapFadePass::StepFadeOut(int32_t step)
{
    if (!m_initialized)
    {
        WARNLOG("GLMapFadePass: GPU init failed, skipping fade");
        int32_t next = step - 4;
        return (next < 0) ? 0 : next;
    }

    // Capture on the first call (step == 32).
    // Same deferred-capture logic as StepFadeIn: set m_capture_pending and let
    // RenderGPUComposePass() (render thread) call CaptureAndUploadFrames().
    if (step == 32)
    {
        m_deactivate_after_render = false;
        m_capture_pending = true;
        m_active = true;
    }

    // Derive step from instance_remain_turns and interpolate between game
    // ticks for smooth fade at display refresh rate.
    //   Base formula: remain * 4   →  28 at remain=7 … 4 at remain=1
    //   Interpolated: (remain - frac) * 4  where frac ∈ [0,1)
    float remain = (float)get_my_player()->instance_remain_turns;
    float frac = (is_feature_on(Ft_DeltaTime) && remain > 0.f)
               ? (float)game.process_turn_time : 0.f;
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    float derived = (remain - frac) * 4.f;
    if (derived < 0.f) derived = 0.f;
    if (derived > 32.f) derived = 32.f;
    m_step = derived;

    return (int32_t)derived;
}

/******************************************************************************/

void GLMapFadePass::RenderGPUComposePass()
{
    if (!m_active || !m_prog || !m_vao)
        return;

    // Deferred capture: CaptureAndUploadFrames() makes GL calls and must run on
    // the render thread that owns the GL context.  StepFadeIn/Out set
    // m_capture_pending on the game thread; we consume it here.
    if (m_capture_pending)
    {
        m_capture_pending = false;
        if (!CaptureAndUploadFrames())
        {
            WARNLOG("GLMapFadePass: capture failed in RenderGPUComposePass, disabling");
            m_active = false;
            return;
        }
    }

    // Safety: if the game has already ended the transition (the end callback
    // restored view_mode before we finished stepping), render one final frame
    // at the terminal step value (step=0 for fade-out = 100% world, step=32
    // for fade-in = 100% parchment) so the compose output seamlessly matches
    // the live view.  Deactivate after this frame via m_deactivate_after_render.
    {
        struct PlayerInfo* player = get_my_player();
        if (player->view_mode != PVM_ParchFadeIn &&
            player->view_mode != PVM_ParchFadeOut)
        {
            if (!m_deactivate_after_render)
            {
                // First frame after instance ended: snap to terminal step
                // and mark for deactivation after this render.
                // Fade-out ends at step=0 (100% world), fade-in at step=32 (100% parchment).
                // We can tell which by looking at m_step: if it was decreasing
                // toward 0 it was a fade-out; if increasing toward 32, fade-in.
                m_step = (m_step <= 16.f) ? 0.f : 32.f;
                m_deactivate_after_render = true;
            }
            else
            {
                // Already rendered the terminal frame last pass — deactivate now.
                m_active = false;
                m_deactivate_after_render = false;
                return;
            }
        }
    }

    // The compose quad covers the entire screen.  The world viewport is
    // already fullscreen in GL mode, so no explicit glViewport override needed.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(m_prog);
    glUniform1f(m_loc_step, m_step);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex[0]);  // parchment
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_tex[1]);  // 3D world

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);

    // Deactivate after rendering the final step so the compose pass
    // actually draws the last transition frame before going inactive.
    if (m_deactivate_after_render)
    {
        m_active = false;
        m_deactivate_after_render = false;
    }
}

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
