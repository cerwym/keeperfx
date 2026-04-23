/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLMapFadePass.cpp
 *     Desktop OpenGL GPU implementation of the map-fade transition.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLMapFadePass.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLShaders.h"
#include "bflib_video.h"      // lbPalette, PALETTE_COLORS, MyScreenWidth/Height, pixel_size
#include "engine_render.h"    // poly_pool, PALETTE_COLORS
#include "engine_redraw.h"    // prepare_map_fade_buffers, map_fade_in, map_fade_out

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
    Init();
}

GLMapFadePass::~GLMapFadePass()
{
    Shutdown();
}

bool GLMapFadePass::Init()
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
    if (!poly_pool)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — poly_pool not initialised");
        return false;
    }

    // ----- Capture both views into 320 × (MyScreenHeight/pixel_size) CPU buffers -----
    // Layout in poly_pool matches map_fade_in / map_fade_out:
    //   offset 0                         — ghost table (PALETTE_COLORS²)
    //   offset PALETTE_COLORS²           — src  = 3D   world view
    //   offset PALETTE_COLORS² + 320*200 — dest = parchment view
    unsigned char* fade_src  = poly_pool + (PALETTE_COLORS * PALETTE_COLORS);
    unsigned char* fade_dest = fade_src  + 320 * 200;

    int tex_w = MyScreenWidth  / pixel_size;
    int tex_h = MyScreenHeight / pixel_size;
    if (tex_w < 1 || tex_h < 1)
    {
        WARNLOG("GLMapFadePass::CaptureAndUploadFrames — degenerate screen size %dx%d", tex_w, tex_h);
        return false;
    }

    prepare_map_fade_buffers(fade_src, fade_dest, 320, tex_h);

    m_tex_w = tex_w;
    m_tex_h = tex_h;

    // ----- Decode palette indices to RGBA8 and upload -----
    std::vector<uint8_t> rgba(tex_w * tex_h * 4);

    // [0] = parchment (fade_dest), [1] = 3D world (fade_src)
    const unsigned char* cpu_bufs[2] = { fade_dest, fade_src };

    for (int b = 0; b < 2; ++b)
    {
        const unsigned char* src = cpu_bufs[b];
        for (int row = 0; row < tex_h; ++row)
        {
            for (int col = 0; col < tex_w; ++col)
            {
                uint8_t idx = src[row * 320 + col];
                rgba[(row * tex_w + col) * 4 + 0] = (uint8_t)(lbPalette[idx * 3 + 0] << 2);
                rgba[(row * tex_w + col) * 4 + 1] = (uint8_t)(lbPalette[idx * 3 + 1] << 2);
                rgba[(row * tex_w + col) * 4 + 2] = (uint8_t)(lbPalette[idx * 3 + 2] << 2);
                rgba[(row * tex_w + col) * 4 + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, m_tex[b]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tex_w, tex_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void GLMapFadePass::MarkDone()
{
    m_active = false;
    m_step   = 0;
}

/******************************************************************************/

int32_t GLMapFadePass::StepFadeIn(int32_t step)
{
    if (!m_initialized && !Init())
    {
        // GPU init failed; delegate to software path.
        return map_fade_in(step);
    }

    if (step == 0)
    {
        if (!CaptureAndUploadFrames())
        {
            WARNLOG("GLMapFadePass: capture failed on fade-in, using software fallback");
            m_active = false;
            return map_fade_in(step);
        }
        m_active = true;
    }
    else if (!m_active)
    {
        // Shouldn't happen in normal flow, but guard gracefully.
        return map_fade_in(step);
    }

    m_step = step;

    int32_t next = step + 4;
    if (next > 32) next = 32;
    return next;
}

int32_t GLMapFadePass::StepFadeOut(int32_t step)
{
    if (!m_initialized && !Init())
    {
        return map_fade_out(step);
    }

    if (step == 32)
    {
        if (!CaptureAndUploadFrames())
        {
            WARNLOG("GLMapFadePass: capture failed on fade-out, using software fallback");
            m_active = false;
            return map_fade_out(step);
        }
        m_active = true;
    }
    else if (!m_active)
    {
        return map_fade_out(step);
    }

    m_step = step;

    int32_t next = step - 4;
    if (next < 0) next = 0;
    return next;
}

/******************************************************************************/

void GLMapFadePass::RenderGPUComposePass()
{
    if (!m_active || !m_prog || !m_vao)
        return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(m_prog);
    glUniform1f(m_loc_step, (float)m_step);

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
