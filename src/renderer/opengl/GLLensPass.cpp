/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLLensPass.cpp
 *     OpenGL implementations of IPostProcessPass for lens effects.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "globals.h"
#include "renderer/opengl/GLLensPass.h"
#include "renderer/opengl/GLShaders.h"
#include "bflib_basics.h"

#include <glad/glad.h>
#include <cmath>
#include <string>
#include "post_inc.h"

/******************************************************************************/
// Fullscreen quad vertices: pos(xy) + uv
static const float k_fsQuad[] = {
    -1.f, -1.f,  0.f, 1.f,
     1.f, -1.f,  1.f, 1.f,
     1.f,  1.f,  1.f, 0.f,
    -1.f, -1.f,  0.f, 1.f,
     1.f,  1.f,  1.f, 0.f,
    -1.f,  1.f,  0.f, 0.f,
};

static unsigned int compile_pass_shader(GLenum type, const char* src)
{
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ERRORLOG("GLLensPass shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/
// GLLensPassBase
/******************************************************************************/

GLLensPassBase::~GLLensPassBase()
{
    Free();
}

void GLLensPassBase::Free()
{
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
    if (m_vao)  { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo)  { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
}

unsigned int GLLensPassBase::CompileProgram(const char* frag_src)
{
    unsigned int vs = compile_pass_shader(GL_VERTEX_SHADER,   PALETTE_BLIT_VERTEX_SHADER);
    unsigned int fs = compile_pass_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        ERRORLOG("GLLensPass link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }

    // Bind u_texture sampler to unit 0 (all passes use this).
    glUseProgram(prog);
    int loc = glGetUniformLocation(prog, "u_texture");
    if (loc >= 0) glUniform1i(loc, 0);
    glUseProgram(0);
    return prog;
}

bool GLLensPassBase::CompilePass(const char* frag_src)
{
    m_prog = CompileProgram(frag_src);
    if (!m_prog)
        return false;

    // Create fullscreen quad VAO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_fsQuad), k_fsQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    return true;
}

bool GLLensPassBase::EnsureCompiled()
{
    if (m_prog)
        return true;
    if (m_compile_attempted)
        return false;
    m_compile_attempted = true;
    return CompileShaders();
}

void GLLensPassBase::BindPass(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPassProgram(m_prog, src_tex, dst_fbo, w, h);
}

void GLLensPassBase::BindPassProgram(unsigned int prog, unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);

    glUseProgram(prog);
    glBindVertexArray(m_vao);
}

void GLLensPassBase::DrawPass()
{
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

/******************************************************************************/
// GLRemapPass  (shared by Displacement + Flyeye)
/******************************************************************************/

GLRemapPass::~GLRemapPass()
{
    Free();
}

bool GLRemapPass::CompileShaders()
{
    if (!CompilePass(LENS_REMAP_FRAGMENT_SHADER))
        return false;
    m_loc_remap_dim = glGetUniformLocation(m_prog, "u_remap_dim");
    // Bind the remap sampler to texture unit 1 (scene stays on unit 0).
    int loc = glGetUniformLocation(m_prog, "u_remap");
    if (loc >= 0)
    {
        glUseProgram(m_prog);
        glUniform1i(loc, 1);
        glUseProgram(0);
    }
    return true;
}

bool GLRemapPass::Init()
{
    // Deferred to EnsureCompiled() on first Apply() — see GLDisplacementPass note.
    return true;
}

bool GLRemapPass::UploadRemap(const unsigned char* rg16, int w, int h)
{
    if (rg16 == nullptr || w <= 0 || h <= 0)
        return false;
    if (!m_remap_tex)
        glGenTextures(1, &m_remap_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_remap_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Integer texture: two uint16 per texel = (src_x, src_y) pixel coordinates.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16UI, w, h, 0,
                 GL_RG_INTEGER, GL_UNSIGNED_SHORT, rg16);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    m_remap_w = w;
    m_remap_h = h;
    return true;
}

void GLRemapPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    if (!EnsureCompiled()) return;

    // Deferred render-thread upload of the remap table staged by Configure() on
    // the game thread (GL uploads must not run there).
    if (m_remap_dirty)
    {
        if (UploadRemap(m_pending_remap, m_pending_w, m_pending_h))
            m_uploaded_version = m_pending_version;
        m_remap_dirty = false;
    }

    // Without a remap texture there is nothing to resample through — skip so the
    // scene passes through untouched rather than sampling an unbound unit.
    if (!m_remap_tex) return;

    BindPass(src_tex, dst_fbo, w, h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_remap_tex);
    if (m_loc_remap_dim >= 0)
        glUniform2f(m_loc_remap_dim, (float)m_remap_w, (float)m_remap_h);
    DrawPass();
    glActiveTexture(GL_TEXTURE0);
}

void GLRemapPass::Free()
{
    if (m_remap_tex) { glDeleteTextures(1, &m_remap_tex); m_remap_tex = 0; }
    m_uploaded_version = 0;
    m_remap_dirty = false;
    GLLensPassBase::Free();
}

/******************************************************************************/
// GLMistPass
/******************************************************************************/

GLMistPass::~GLMistPass()
{
    Free();
}

bool GLMistPass::CompileShaders()
{
    // Default (truecolor) program + shared VAO.
    if (!CompilePass(LENS_MIST_FRAGMENT_SHADER))
        return false;
    m_loc_pos        = glGetUniformLocation(m_prog, "u_pos");
    m_loc_sec        = glGetUniformLocation(m_prog, "u_sec");
    m_loc_mist_color = glGetUniformLocation(m_prog, "u_mist_color");
    // Bind the mist amplitude sampler to texture unit 1 (scene stays on unit 0).
    int loc = glGetUniformLocation(m_prog, "u_mist");
    if (loc >= 0)
    {
        glUseProgram(m_prog);
        glUniform1i(loc, 1);
        glUseProgram(0);
    }

    // Accurate (paletted) program — bit-exact with software. Optional: if it fails
    // to compile we simply fall back to the truecolor path (AccurateReady() gates it).
    m_prog_accurate = CompileProgram(LENS_MIST_ACCURATE_FRAGMENT_SHADER);
    if (m_prog_accurate)
    {
        glUseProgram(m_prog_accurate);
        int l;
        if ((l = glGetUniformLocation(m_prog_accurate, "u_mist"))    >= 0) glUniform1i(l, 1);
        if ((l = glGetUniformLocation(m_prog_accurate, "u_rgb2idx")) >= 0) glUniform1i(l, 2);
        if ((l = glGetUniformLocation(m_prog_accurate, "u_fade"))    >= 0) glUniform1i(l, 3);
        if ((l = glGetUniformLocation(m_prog_accurate, "u_palette")) >= 0) glUniform1i(l, 4);
        glUseProgram(0);
        m_loc_acc_pos       = glGetUniformLocation(m_prog_accurate, "u_pos");
        m_loc_acc_sec       = glGetUniformLocation(m_prog_accurate, "u_sec");
        m_loc_acc_lightness = glGetUniformLocation(m_prog_accurate, "u_mist_lightness");
    }
    return true;
}

bool GLMistPass::Init()
{
    // Deferred to EnsureCompiled() on first Apply().
    return true;
}

bool GLMistPass::UploadMist(const unsigned char* r8)
{
    if (r8 == nullptr)
        return false;
    if (!m_mist_tex)
        glGenTextures(1, &m_mist_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_mist_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // Single-channel amplitude — replicate to .r via GL_R8.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0,
                 GL_RED, GL_UNSIGNED_BYTE, r8);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    return true;
}

void GLMistPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    if (!EnsureCompiled()) return;

    if (m_mist_dirty)
    {
        UploadMist(m_pending_mist);
        m_mist_dirty = false;
    }
    if (!m_mist_tex) return;  // nothing to blend through yet

    // Offsets are advanced on the game thread (frame-rate independent) and pushed
    // in via Configure() each frame — the pass just consumes them here.
    if (AccurateReady())
    {
        // Bit-exact paletted fade: recover each pixel's palette index (unit 2),
        // run the DK fade table (unit 3), re-decode via the palette (unit 4).
        BindPassProgram(m_prog_accurate, src_tex, dst_fbo, w, h);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_mist_tex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_3D, m_tex_rgb2idx);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m_tex_fade);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, m_tex_palette);
        if (m_loc_acc_pos >= 0)       glUniform2f(m_loc_acc_pos, m_pos_x, m_pos_y);
        if (m_loc_acc_sec >= 0)       glUniform2f(m_loc_acc_sec, m_sec_x, m_sec_y);
        if (m_loc_acc_lightness >= 0) glUniform1f(m_loc_acc_lightness, (float)m_mist_lightness);
        DrawPass();
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_3D, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        return;
    }

    // Truecolor path: smooth RGBA blend toward the fog "dirt" colour.
    BindPass(src_tex, dst_fbo, w, h);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_mist_tex);
    if (m_loc_pos >= 0)        glUniform2f(m_loc_pos, m_pos_x, m_pos_y);
    if (m_loc_sec >= 0)        glUniform2f(m_loc_sec, m_sec_x, m_sec_y);
    if (m_loc_mist_color >= 0) glUniform4fv(m_loc_mist_color, 1, m_color);
    DrawPass();
    glActiveTexture(GL_TEXTURE0);
}

void GLMistPass::Free()
{
    if (m_mist_tex) { glDeleteTextures(1, &m_mist_tex); m_mist_tex = 0; }
    if (m_prog_accurate) { glDeleteProgram(m_prog_accurate); m_prog_accurate = 0; }
    m_mist_dirty = false;
    GLLensPassBase::Free();
}

/******************************************************************************/
// GLOverlayPass
/******************************************************************************/

bool GLOverlayPass::CompileShaders()
{
    if (!CompilePass(LENS_OVERLAY_FRAGMENT_SHADER))
        return false;
    m_loc_overlay_alpha = glGetUniformLocation(m_prog, "u_overlay_alpha");
    // Bind overlay sampler to unit 1
    int loc = glGetUniformLocation(m_prog, "u_overlay");
    if (loc >= 0)
    {
        glUseProgram(m_prog);
        glUniform1i(loc, 1);
        glUseProgram(0);
    }
    // 1×1 transparent RGBA8 fallback — ensures unit 1 always has a complete
    // texture bound even when no overlay image has been uploaded.
    if (!m_null_overlay_tex)
    {
        glGenTextures(1, &m_null_overlay_tex);
        glBindTexture(GL_TEXTURE_2D, m_null_overlay_tex);
        const uint8_t null_px[4] = {0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, null_px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    return true;
}

bool GLOverlayPass::Init()
{
    // Deferred to EnsureCompiled() on first Apply() — see GLDisplacementPass::Init().
    return true;
}

void GLOverlayPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    if (!EnsureCompiled()) return;
    // Deferred render-thread upload of the overlay image staged by Configure()
    // on the game thread (GL uploads must not run there).
    if (m_overlay_dirty)
    {
        if (m_pending_overlay != nullptr)
            UploadOverlay(m_pending_overlay, m_pending_w, m_pending_h);
        m_overlay_dirty = false;
    }
    BindPass(src_tex, dst_fbo, w, h);
    // Always bind a valid texture to unit 1 — use the uploaded overlay when
    // available, otherwise the 1×1 transparent fallback so the sampler is never
    // left pointing at an unbound or stale texture unit.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_overlay_tex ? m_overlay_tex : m_null_overlay_tex);
    if (m_loc_overlay_alpha >= 0) glUniform1f(m_loc_overlay_alpha, m_alpha);
    DrawPass();
    glActiveTexture(GL_TEXTURE0);
}

void GLOverlayPass::Free()
{
    if (m_null_overlay_tex) { glDeleteTextures(1, &m_null_overlay_tex); m_null_overlay_tex = 0; }
    if (m_overlay_tex)      { glDeleteTextures(1, &m_overlay_tex);      m_overlay_tex = 0; }
    GLLensPassBase::Free();
}

bool GLOverlayPass::UploadOverlay(const unsigned char* rgba, int w, int h)
{
    if (!m_overlay_tex)
    {
        glGenTextures(1, &m_overlay_tex);
        glBindTexture(GL_TEXTURE_2D, m_overlay_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, m_overlay_tex);
    }

    if (w != m_overlay_w || h != m_overlay_h)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        m_overlay_w = w;
        m_overlay_h = h;
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

/******************************************************************************/
