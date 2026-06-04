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
#include "renderer/RenderGraph.h"
#include "renderer/ir/PostProcessCommands.h"
#include "engine_redraw.h"    // setup_engine_window
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

GLMapFadePass::GLMapFadePass(RendererOpenGL* renderer)
    : m_renderer(renderer)
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
    if (!m_renderer)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — no renderer bound");
        return false;
    }

    const int w = m_screen_w;
    const int h = m_screen_h;
    if (w < 1 || h < 1)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — degenerate screen size %dx%d", w, h);
        return false;
    }

    m_tex_w = w;
    m_tex_h = h;

    // Resize both snapshot textures to the window resolution.
    for (int i = 0; i < 2; ++i)
    {
        glBindTexture(GL_TEXTURE_2D, m_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Capture 3D world view → m_tex[1] via glBlitFramebuffer ──────────
    // The dungeon geometry was already rendered to the default framebuffer
    // by ExecuteWorldFromIR() before CaptureAndUploadFrames is called.
    // We blit the current default FB content into m_tex[1] directly, which
    // avoids re-running draw_view() on the render thread (unsafe — races with
    // the game thread building the next frame).
    {
        GLuint blit_fbo = 0;
        glGenFramebuffers(1, &blit_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blit_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tex[1], 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            WARNLOG("GLMapFadePass: world-capture blit FBO incomplete — world snapshot skipped");
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &blit_fbo);
            return false;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); // default framebuffer
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &blit_fbo);
    }

    // ── Capture parchment overhead view → m_tex[0] ──────────────────────
    // The parchment rawblit + overhead map were submitted to the pending
    // command queues by StepFadeIn()/StepFadeOut() on the game thread.
    // Render them into a temporary FBO attached to m_tex[0].
    // FlushSceneToFBO() consumes and clears m_rt_rawblit_pending and
    // m_rt_overhead_map_cmds so the main EndFrame_GL pass will skip them.
    {
        GLuint fbo = 0, depth_rb = 0;
        glGenFramebuffers(1, &fbo);
        glGenRenderbuffers(1, &depth_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_tex[0], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, depth_rb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            WARNLOG("GLMapFadePass: parchment capture FBO incomplete — parchment snapshot skipped");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteRenderbuffers(1, &depth_rb);
            glDeleteFramebuffers(1, &fbo);
            return false;
        }

        float saved_cc[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_cc);

        glViewport(0, 0, w, h);
        glClearColor(0, 0, 0, 1);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_renderer->FlushSceneToFBO(w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteRenderbuffers(1, &depth_rb);
        glDeleteFramebuffers(1, &fbo);

        glClearColor(saved_cc[0], saved_cc[1], saved_cc[2], saved_cc[3]);
        glDepthMask(GL_FALSE);
    }

    glViewport(0, 0, w, h);
    return true;
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

    // Arm capture on the first call (step == 0).
    // Submit dungeon + parchment render data NOW on the game thread so the
    // render thread has world geometry in the write buffers when it runs
    // ExecuteWorldFromIR(), and has rawblit/overhead queued for parchment capture.
    if (step == 0)
    {
        struct PlayerInfo* player = get_my_player();
        struct EngineRenderState saved_state;
        engine_save_render_state(&saved_state);
        setup_engine_window(0, 0, m_screen_w, m_screen_h);
        WorldViewRenderer_BeginWorldPass(nullptr, 0, m_screen_w, m_screen_h, 0, 0);
        struct Camera* cam;
        if (player->view_mode_restore == PVM_IsoWibbleView ||
            player->view_mode_restore == PVM_IsoStraightView)
            cam = get_local_camera(CamIV_Isometric);
        else
            cam = get_local_camera(CamIV_FrontView);
        draw_view(cam, 0);
        engine_restore_render_state(&saved_state);
        load_parchment_file();
        redraw_minimal_overhead_view();
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

    // Arm capture on the first call (step == 32).
    // Same game-thread data submission as StepFadeIn: submit dungeon + parchment
    // render data so the render thread has everything it needs for capture.
    if (step == 32)
    {
        struct PlayerInfo* player = get_my_player();
        struct EngineRenderState saved_state;
        engine_save_render_state(&saved_state);
        setup_engine_window(0, 0, m_screen_w, m_screen_h);
        WorldViewRenderer_BeginWorldPass(nullptr, 0, m_screen_w, m_screen_h, 0, 0);
        struct Camera* cam;
        if (player->view_mode_restore == PVM_IsoWibbleView ||
            player->view_mode_restore == PVM_IsoStraightView)
            cam = get_local_camera(CamIV_Isometric);
        else
            cam = get_local_camera(CamIV_FrontView);
        draw_view(cam, 0);
        engine_restore_render_state(&saved_state);
        load_parchment_file();
        redraw_minimal_overhead_view();
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

void GLMapFadePass::FlushToRenderGraph(RenderGraph& graph)
{
    if (!m_active)
    {
        graph.GetPostProcessBuffers().map_fade = std::nullopt;
        return;
    }

    // If the game has already ended the transition (view_mode left ParchFade
    // mode), snap to the terminal step and emit one final frame so the
    // compose output seamlessly matches the live view.  m_active becomes false
    // so subsequent EndFrame() calls clear the map-fade cmd automatically.
    struct PlayerInfo* player = get_my_player();
    if (player->view_mode != PVM_ParchFadeIn && player->view_mode != PVM_ParchFadeOut)
    {
        m_step   = (m_step <= 16.f) ? 0.f : 32.f;
        m_active = false;
    }

    // Transfer the pending-capture flag to the IR command and clear it here
    // so the render thread receives it exactly once.
    graph.GetPostProcessBuffers().map_fade = IRMapFadeCmd{m_step, m_capture_pending};
    m_capture_pending = false;
}

void GLMapFadePass::ExecuteFromIR(const IRMapFadeCmd& cmd)
{
    if (!m_prog || !m_vao)
        return;

    if (cmd.capture_pending)
    {
        if (!CaptureAndUploadFrames())
        {
            WARNLOG("GLMapFadePass: capture failed in ExecuteFromIR, skipping");
            return;
        }
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(m_prog);
    glUniform1f(m_loc_step, cmd.step);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex[0]);  // parchment
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_tex[1]);  // 3D world

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
}

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
