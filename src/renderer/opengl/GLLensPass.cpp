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
#include "renderer/opengl/GLShaderLoader.h"
#include "renderer/opengl/GLShaders.h"
#include "bflib_basics.h"

#include <glad/glad.h>
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

bool GLLensPassBase::CompilePass(const char* frag_shader_name)
{
    std::string vert_src = get_embedded_shader_source("palette_blit_vert.glsl");
    std::string frag_src = get_embedded_shader_source(frag_shader_name);
    if (vert_src.empty() || frag_src.empty())
    {
        ERRORLOG("GLLensPass: shader source '%s' not found", frag_shader_name);
        return false;
    }

    unsigned int vs = compile_pass_shader(GL_VERTEX_SHADER, vert_src.c_str());
    unsigned int fs = compile_pass_shader(GL_FRAGMENT_SHADER, frag_src.c_str());
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(m_prog, sizeof(log), nullptr, log);
        ERRORLOG("GLLensPass link error: %s", log);
        glDeleteProgram(m_prog);
        m_prog = 0;
        return false;
    }

    // Bind u_texture sampler to unit 0 (all passes use this).
    glUseProgram(m_prog);
    int loc = glGetUniformLocation(m_prog, "u_texture");
    if (loc >= 0) glUniform1i(loc, 0);

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

void GLLensPassBase::BindPass(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);

    glUseProgram(m_prog);
    glBindVertexArray(m_vao);
}

void GLLensPassBase::DrawPass()
{
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

/******************************************************************************/
// GLDisplacementPass
/******************************************************************************/

bool GLDisplacementPass::Init()
{
    if (!CompilePass("lens_displacement_frag.glsl"))
        return false;
    m_loc_time      = glGetUniformLocation(m_prog, "u_time");
    m_loc_magnitude = glGetUniformLocation(m_prog, "u_magnitude");
    m_loc_period    = glGetUniformLocation(m_prog, "u_period");
    return true;
}

void GLDisplacementPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPass(src_tex, dst_fbo, w, h);
    m_time += 0.016f; // ~60fps tick
    if (m_loc_time >= 0)      glUniform1f(m_loc_time, m_time);
    if (m_loc_magnitude >= 0) glUniform1f(m_loc_magnitude, m_magnitude);
    if (m_loc_period >= 0)    glUniform1f(m_loc_period, m_period);
    DrawPass();
}

/******************************************************************************/
// GLMistPass
/******************************************************************************/

bool GLMistPass::Init()
{
    if (!CompilePass("lens_mist_frag.glsl"))
        return false;
    m_loc_time       = glGetUniformLocation(m_prog, "u_time");
    m_loc_mist_color = glGetUniformLocation(m_prog, "u_mist_color");
    return true;
}

void GLMistPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPass(src_tex, dst_fbo, w, h);
    m_time += 0.016f;
    if (m_loc_time >= 0)       glUniform1f(m_loc_time, m_time);
    if (m_loc_mist_color >= 0) glUniform4fv(m_loc_mist_color, 1, m_color);
    DrawPass();
}

/******************************************************************************/
// GLFlyeyePass
/******************************************************************************/

bool GLFlyeyePass::Init()
{
    if (!CompilePass("lens_flyeye_frag.glsl"))
        return false;
    m_loc_hex_size   = glGetUniformLocation(m_prog, "u_hex_size");
    m_loc_resolution = glGetUniformLocation(m_prog, "u_resolution");
    return true;
}

void GLFlyeyePass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPass(src_tex, dst_fbo, w, h);
    if (m_loc_hex_size >= 0)   glUniform1f(m_loc_hex_size, m_hex_size);
    if (m_loc_resolution >= 0) glUniform2f(m_loc_resolution, (float)w, (float)h);
    DrawPass();
}

/******************************************************************************/
// GLPalettePass
/******************************************************************************/

bool GLPalettePass::Init()
{
    if (!CompilePass("lens_palette_frag.glsl"))
        return false;
    m_loc_color_shift = glGetUniformLocation(m_prog, "u_color_shift");
    return true;
}

void GLPalettePass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPass(src_tex, dst_fbo, w, h);
    if (m_loc_color_shift >= 0) glUniform3fv(m_loc_color_shift, 1, m_shift);
    DrawPass();
}

/******************************************************************************/
// GLOverlayPass
/******************************************************************************/

bool GLOverlayPass::Init()
{
    if (!CompilePass("lens_overlay_frag.glsl"))
        return false;
    m_loc_overlay_alpha = glGetUniformLocation(m_prog, "u_overlay_alpha");
    // Bind overlay sampler to unit 1
    int loc = glGetUniformLocation(m_prog, "u_overlay");
    if (loc >= 0)
    {
        glUseProgram(m_prog);
        glUniform1i(loc, 1);
    }
    return true;
}

void GLOverlayPass::Apply(unsigned int src_tex, unsigned int dst_fbo, int w, int h)
{
    BindPass(src_tex, dst_fbo, w, h);
    if (m_overlay_tex)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_overlay_tex);
    }
    if (m_loc_overlay_alpha >= 0) glUniform1f(m_loc_overlay_alpha, m_alpha);
    DrawPass();
    glActiveTexture(GL_TEXTURE0);
}

void GLOverlayPass::Free()
{
    if (m_overlay_tex) { glDeleteTextures(1, &m_overlay_tex); m_overlay_tex = 0; }
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
