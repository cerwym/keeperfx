/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLLensRenderer.cpp
 *     OpenGL implementation of ILensRenderer. See header for the design.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLLensRenderer.h"
#include "renderer/opengl/GLLensPass.h"
#include "renderer/opengl/GLShaders.h"          // PALETTE_BLIT_VERTEX_SHADER / PASSTHROUGH_FRAGMENT_SHADER
#include "renderer/RendererOpenGL.h"            // back-pointer: m_vao, upload_palette_buffer()
#include "renderer/ir/PostProcessCommands.h"    // IRLensCmd / LensScope

#include <cstring>
#include "globals.h"                            // SYNCDBG / ERRORLOG

#include "post_inc.h"

/******************************************************************************/
// File-local FBO helpers (mirrors the pair previously in RendererOpenGL.cpp).
/******************************************************************************/

static unsigned int gl_lens_create_rgba_texture(int w, int h)
{
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static unsigned int gl_lens_create_fbo_with_texture(unsigned int color_tex, unsigned int depth_rb)
{
    unsigned int fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex, 0);
    if (depth_rb)
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        ERRORLOG("Lens FBO incomplete: status=0x%X", status);
        glDeleteFramebuffers(1, &fbo);
        return 0;
    }
    return fbo;
}

/******************************************************************************/

GLLensRenderer::GLLensRenderer(RendererOpenGL* renderer)
    : m_renderer(renderer)
{
}

GLLensRenderer::~GLLensRenderer()
{
    ReleaseAll();
}

IPostProcessPass* GLLensRenderer::CreatePass(LensEffectType type)
{
    switch (type)
    {
        case LensEffectType::Mist:         return new GLMistPass();
        case LensEffectType::Displacement: return new GLDisplacementPass();
        case LensEffectType::Flyeye:       return new GLFlyeyePass();
        case LensEffectType::Overlay:      return new GLOverlayPass();
        default:                           return nullptr;
    }
}

IPostProcessPass* GLLensRenderer::AcquireConfiguredPass(const IRLensEffect& effect)
{
    const int idx = static_cast<int>(effect.type);
    if (idx < 0 || idx >= kSlotCount)
        return nullptr;

    Slot& s = m_slots[idx];
    if (s.pass == nullptr)
    {
        s.pass = CreatePass(effect.type);
        if (s.pass == nullptr)
            return nullptr;
        // GL passes defer real resource creation to EnsureCompiled() inside
        // Apply() (render thread), so Init() here is a cheap no-op.
        s.pass->Init();
    }

    // Resolve the effect's owned pixel payloads into a local params copy. The IR
    // itself never carries a live pointer (they were detached into the owned
    // vectors on the game thread), so nothing here can dangle when the game thread
    // frees the source lens buffers/objects on depossess.
    LensGPUPassParams params = effect.params;
    if (!effect.mist_pixels.empty())    params.mist_data    = effect.mist_pixels.data();
    if (!effect.overlay_pixels.empty()) params.overlay_data = effect.overlay_pixels.data();

    // Re-configure only when the parameters or the owned payload actually change.
    // last_params is compared pointer-free (effect.params has null pointers), and
    // the payload change is caught by the vector comparisons — so a stable overlay
    // is not re-uploaded every frame (GLOverlayPass marks its texture dirty on
    // every Configure()).
    const bool changed =
        !s.configured
        || std::memcmp(&s.last_params, &effect.params, sizeof(effect.params)) != 0
        || s.last_mist    != effect.mist_pixels
        || s.last_overlay != effect.overlay_pixels;
    if (changed)
    {
        s.pass->Configure(params);
        s.last_params  = effect.params;   // pointer-free snapshot for next-frame compare
        s.last_mist    = effect.mist_pixels;
        s.last_overlay = effect.overlay_pixels;
        s.configured   = true;
    }

    return s.pass;
}

void GLLensRenderer::EnsureFBOs(int w, int h)
{
    if (m_lens_fbo_w == w && m_lens_fbo_h == h && m_lens_scene_fbo)
        return;

    // Free existing
    DestroyFBOs();

    // Scene FBO — world renders here instead of default framebuffer
    m_lens_scene_tex = gl_lens_create_rgba_texture(w, h);

    glGenRenderbuffers(1, &m_lens_scene_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_lens_scene_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    m_lens_scene_fbo = gl_lens_create_fbo_with_texture(m_lens_scene_tex, m_lens_scene_depth_rb);

    // Ping-pong FBOs (no depth needed — post-process only)
    m_lens_pass_tex_a = gl_lens_create_rgba_texture(w, h);
    m_lens_pass_fbo_a = gl_lens_create_fbo_with_texture(m_lens_pass_tex_a, 0);

    m_lens_pass_tex_b = gl_lens_create_rgba_texture(w, h);
    m_lens_pass_fbo_b = gl_lens_create_fbo_with_texture(m_lens_pass_tex_b, 0);

    m_lens_fbo_w = w;
    m_lens_fbo_h = h;

    // Compile passthrough shader (blit final texture to screen)
    if (!m_passthrough_shader)
    {
        auto cs = [](GLenum type, const char* src) -> unsigned int {
            unsigned int s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            int ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) { glDeleteShader(s); return 0; }
            return s;
        };
        unsigned int vs = cs(GL_VERTEX_SHADER,   PALETTE_BLIT_VERTEX_SHADER);
        unsigned int fs = cs(GL_FRAGMENT_SHADER, PASSTHROUGH_FRAGMENT_SHADER);
        if (vs && fs)
        {
            m_passthrough_shader = glCreateProgram();
            glAttachShader(m_passthrough_shader, vs);
            glAttachShader(m_passthrough_shader, fs);
            glLinkProgram(m_passthrough_shader);
            glUseProgram(m_passthrough_shader);
            glUniform1i(glGetUniformLocation(m_passthrough_shader, "u_texture"), 0);
        }
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
    }

    SYNCDBG(7, "Lens FBOs created: %dx%d (scene=%u passA=%u passB=%u)",
            w, h, m_lens_scene_fbo, m_lens_pass_fbo_a, m_lens_pass_fbo_b);
}

void GLLensRenderer::DestroyFBOs()
{
    if (m_lens_scene_fbo)      { glDeleteFramebuffers(1, &m_lens_scene_fbo);       m_lens_scene_fbo = 0; }
    if (m_lens_scene_tex)      { glDeleteTextures(1, &m_lens_scene_tex);           m_lens_scene_tex = 0; }
    if (m_lens_scene_depth_rb) { glDeleteRenderbuffers(1, &m_lens_scene_depth_rb); m_lens_scene_depth_rb = 0; }
    if (m_lens_pass_fbo_a)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_a);      m_lens_pass_fbo_a = 0; }
    if (m_lens_pass_tex_a)     { glDeleteTextures(1, &m_lens_pass_tex_a);          m_lens_pass_tex_a = 0; }
    if (m_lens_pass_fbo_b)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_b);      m_lens_pass_fbo_b = 0; }
    if (m_lens_pass_tex_b)     { glDeleteTextures(1, &m_lens_pass_tex_b);          m_lens_pass_tex_b = 0; }
    if (m_passthrough_shader)  { glDeleteProgram(m_passthrough_shader);            m_passthrough_shader = 0; }
    m_lens_fbo_w = 0;
    m_lens_fbo_h = 0;
}

bool GLLensRenderer::BeginSceneCapture(const IRLensCmd& cmd, int w, int h)
{
    if (cmd.count <= 0)
        return false;

    EnsureFBOs(w, h);
    if (!m_lens_scene_fbo)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, m_lens_scene_fbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return true;
}

void GLLensRenderer::ResolveAndApply(const IRLensCmd& cmd, int w, int h)
{
    if (cmd.count <= 0)
        return;

    EnsureFBOs(w, h);
    if (!m_lens_pass_fbo_a || !m_lens_pass_fbo_b)
        return;

    // Ping-pong: scene_tex → pass_a → pass_b → ...
    unsigned int src_tex = m_lens_scene_tex;
    bool flip = false;
    for (int i = 0; i < cmd.count; ++i)
    {
        // The IR carries pure, self-contained data (effect type + params + owned
        // pixel payload); this backend-owned renderer maps it to a configured,
        // cached pass object without dereferencing any game-thread memory.
        IPostProcessPass* pass = AcquireConfiguredPass(cmd.effects[i]);
        if (pass == nullptr)
            continue;
        unsigned int dst_fbo = flip ? m_lens_pass_fbo_b : m_lens_pass_fbo_a;
        unsigned int dst_tex = flip ? m_lens_pass_tex_b : m_lens_pass_tex_a;
        pass->Apply(src_tex, dst_fbo, w, h);
        src_tex = dst_tex;
        flip = !flip;
    }

    // Blit final result to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUseProgram(m_passthrough_shader);
    glBindVertexArray(m_renderer->m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

void GLLensRenderer::ApplyPaletteUIExclusion(const IRLensCmd& cmd)
{
    // By this point the world has been fully decoded to RGBA using the applied
    // (lens-tinted) global palette. If a lens palette is active and its scope is
    // WorldOnly, re-upload the shared palette texture with the BASE (non-lens)
    // palette so the subsequent UI/text/overhead draws decode without the lens
    // tint — leaving the world tinted but the UI clean. When the scope is
    // FullFrame (cfg toggle to include UI) or no lens palette is active, we leave
    // the applied palette in place → identical to legacy.
    if (cmd.has_palette && cmd.palette_scope == LensScope::WorldOnly)
        m_renderer->upload_palette_buffer(cmd.palette.data());
}

void GLLensRenderer::ReleaseAll()
{
    for (Slot& s : m_slots)
    {
        if (s.pass)
        {
            s.pass->Free();
            delete s.pass;
            s.pass = nullptr;
        }
        s.configured = false;
    }
    DestroyFBOs();
}

/******************************************************************************/
#endif // RENDERER_OPENGL_ENABLED
