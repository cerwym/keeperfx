/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file OpenGLSpriteBackend.cpp
 *     Desktop OpenGL sprite backend — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/OpenGLSpriteBackend.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "bflib_video.h"    // lbPalette, lbDisplay, MyScreenWidth/Height, Lb_SPRITE_* flags
#include "bflib_vidraw.h"   // LbSpriteDrawScaledRemap (remap software fallback)
#include "bflib_basics.h"

#include <cstring>
#include "post_inc.h"

/******************************************************************************/

static const char* k_spriteVertSrc = R"(
#version 330 core
layout(location = 0) in vec2  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in vec4  a_tint;
layout(location = 3) in float a_mode;
layout(location = 4) in float a_z;

uniform vec2 u_inv_screen;

out vec2  v_uv;
out vec4  v_tint;
out float v_mode;

void main() {
    vec2 ndc = a_pos * u_inv_screen * 2.0 - vec2(1.0, 1.0);
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, a_z, 1.0);
    v_uv   = a_uv;
    v_tint = a_tint;
    v_mode = a_mode;
}
)";

static const char* k_spriteFragSrc = R"(
#version 330 core
in vec2  v_uv;
in vec4  v_tint;
in float v_mode;

uniform sampler2D u_atlas;
uniform sampler1D u_palette;

out vec4 fragColor;

void main() {
    float idx = texture(u_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    if (v_mode > 0.5) {
        fragColor = v_tint;
    } else {
        fragColor = texture(u_palette, idx) * v_tint;
    }
}
)";

/******************************************************************************/

static GLuint compile_sprite_shader(GLenum type, const char* src)
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
        ERRORLOG("OpenGLSpriteBackend shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/

// Current bucket's NDC depth [-1,+1] to assign to all sprites this flush.
static float s_sprite_z_ndc = 0.0f;

void OpenGLSpriteBackend::SetCurrentBucketZ(float z_ndc)
{
    s_sprite_z_ndc = z_ndc;
}

/******************************************************************************/

OpenGLSpriteBackend::~OpenGLSpriteBackend()
{
    if (m_shader)     { glDeleteProgram(m_shader);          m_shader     = 0; }
    if (m_vao)        { glDeleteVertexArrays(1, &m_vao);    m_vao        = 0; }
    if (m_vbo)        { glDeleteBuffers(1, &m_vbo);         m_vbo        = 0; }
    if (m_texPalette) { glDeleteTextures(1, &m_texPalette); m_texPalette = 0; }
    m_atlas.Free();
}

bool OpenGLSpriteBackend::Initialize()
{
    // ── Shader ──────────────────────────────────────────────────────────────
    GLuint vert = compile_sprite_shader(GL_VERTEX_SHADER,   k_spriteVertSrc);
    GLuint frag = compile_sprite_shader(GL_FRAGMENT_SHADER, k_spriteFragSrc);
    if (!vert || !frag)
    {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    m_shader = glCreateProgram();
    glAttachShader(m_shader, vert);
    glAttachShader(m_shader, frag);
    glLinkProgram(m_shader);
    glDeleteShader(vert);
    glDeleteShader(frag);

    int ok = 0;
    glGetProgramiv(m_shader, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(m_shader, sizeof(log), nullptr, log);
        ERRORLOG("OpenGLSpriteBackend: shader link error: %s", log);
        glDeleteProgram(m_shader);
        m_shader = 0;
        return false;
    }

    m_loc_inv_screen = glGetUniformLocation(m_shader, "u_inv_screen");
    glUseProgram(m_shader);
    glUniform1i(glGetUniformLocation(m_shader, "u_atlas"),   0);
    glUniform1i(glGetUniformLocation(m_shader, "u_palette"), 1);
    glUseProgram(0);

    // ── VAO + VBO ────────────────────────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(k_max_quads * 6 * sizeof(GLSpriteVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = (GLsizei)sizeof(GLSpriteVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);   // a_pos  x,y
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)8);   // a_uv   u,v
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)16);  // a_tint r,g,b,a
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)32);  // a_mode
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride, (void*)36);  // a_z
    glBindVertexArray(0);

    // ── Palette texture (1D, 256 RGBA8 entries) ──────────────────────────────
    glGenTextures(1, &m_texPalette);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_1D, 0);

    // ── Sprite atlas ─────────────────────────────────────────────────────────
    if (!m_atlas.Init())
    {
        ERRORLOG("OpenGLSpriteBackend: atlas init failed");
        return false;
    }

    m_quads.reserve(k_max_quads);
    return true;
}

/******************************************************************************/

void OpenGLSpriteBackend::BeginFrame()
{
    m_quads.clear();
}

void OpenGLSpriteBackend::EndFrame()
{
    if (m_palette_dirty)
    {
        upload_palette();
        m_palette_dirty = false;
    }
    flush();
}

void OpenGLSpriteBackend::FlushNow()
{
    if (m_palette_dirty)
    {
        upload_palette();
        m_palette_dirty = false;
    }
    flush();
}

void OpenGLSpriteBackend::SetScreenSize(int w, int h)
{
    m_override_w = w;
    m_override_h = h;
}

/******************************************************************************/

TbResult OpenGLSpriteBackend::SubmitSprite(long x, long y, const struct TbSprite* spr,
                                           unsigned int draw_flags)
{
    SpriteUV uv;
    if (!m_atlas.GetUV(spr, uv))
        return Lb_FAIL;

    SpriteQuad q;
    q.x0 = (float)x;
    q.y0 = (float)y;
    q.x1 = (float)(x + spr->SWidth);
    q.y1 = (float)(y + spr->SHeight);
    q.u0 = uv.u0; q.v0 = uv.v0;
    q.u1 = uv.u1; q.v1 = uv.v1;
    q.r = 1.0f; q.g = 1.0f; q.b = 1.0f; q.a = 1.0f;
    q.mode = 0.0f;
    q.z = s_sprite_z_ndc;

    // Debug logging to understand coordinate system
    static int debug_count = 0;
    if (debug_count < 5) {
        SYNCLOG("OpenGLSpriteBackend::SubmitSprite: pos=(%ld,%ld) size=(%d,%d) override_screen=(%d,%d) z=%.3f", 
                x, y, spr->SWidth, spr->SHeight, m_override_w, m_override_h, s_sprite_z_ndc);
        debug_count++;
    }

    if (draw_flags & Lb_SPRITE_FLIP_HORIZ)
        std::swap(q.u0, q.u1);

    m_quads.push_back(q);
    if ((int)m_quads.size() >= k_max_quads)
        flush();

    return Lb_OK;
}

TbResult OpenGLSpriteBackend::SubmitSpriteOneColour(long x, long y,
                                                    const struct TbSprite* spr,
                                                    unsigned char colour,
                                                    unsigned int draw_flags)
{
    SpriteUV uv;
    if (!m_atlas.GetUV(spr, uv))
        return Lb_FAIL;

    // Resolve palette index to linear RGBA
    float cr = (float)((int)lbPalette[colour * 3 + 0] << 2) / 255.0f;
    float cg = (float)((int)lbPalette[colour * 3 + 1] << 2) / 255.0f;
    float cb = (float)((int)lbPalette[colour * 3 + 2] << 2) / 255.0f;

    SpriteQuad q;
    q.x0 = (float)x;
    q.y0 = (float)y;
    q.x1 = (float)(x + spr->SWidth);
    q.y1 = (float)(y + spr->SHeight);
    q.u0 = uv.u0; q.v0 = uv.v0;
    q.u1 = uv.u1; q.v1 = uv.v1;
    q.r = cr; q.g = cg; q.b = cb; q.a = 1.0f;
    q.mode = 1.0f;
    q.z = s_sprite_z_ndc;

    if (draw_flags & Lb_SPRITE_FLIP_HORIZ)
        std::swap(q.u0, q.u1);

    m_quads.push_back(q);
    if ((int)m_quads.size() >= k_max_quads)
        flush();

    return Lb_OK;
}

TbResult OpenGLSpriteBackend::SubmitSpriteRemap(long x, long y,
                                                const struct TbSprite* spr,
                                                const unsigned char* colortable,
                                                unsigned int draw_flags)
{
    // Remap sprites require per-pixel palette indirection — fall back to the
    // CPU blitter so they appear in the RendererOpenGL staging buffer blit pass.
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    TbResult ret = LbSpriteDrawScaledRemap(x, y, spr,
                                           spr->SWidth, spr->SHeight, colortable);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

/******************************************************************************/

void OpenGLSpriteBackend::OnSpriteSheetLoaded(const struct TbSpriteSheet* sheet)
{
    m_atlas.AddSheet(sheet);
}

void OpenGLSpriteBackend::OnSpriteSheetFreed(const struct TbSpriteSheet* sheet)
{
    m_atlas.RemoveSheet(sheet);
}

void OpenGLSpriteBackend::OnPaletteSet(const unsigned char* lbPal)
{
    memcpy(m_local_palette, lbPal, 768);
    m_palette_dirty = true;
}

/******************************************************************************/

void OpenGLSpriteBackend::upload_palette()
{
    uint8_t rgba[256 * 4];
    for (int i = 0; i < 256; ++i)
    {
        rgba[i * 4 + 0] = (uint8_t)((int)m_local_palette[i * 3 + 0] << 2);
        rgba[i * 4 + 1] = (uint8_t)((int)m_local_palette[i * 3 + 1] << 2);
        rgba[i * 4 + 2] = (uint8_t)((int)m_local_palette[i * 3 + 2] << 2);
        rgba[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void OpenGLSpriteBackend::flush()
{
    if (m_quads.empty())
        return;

    // Expand SpriteQuads to 6 vertices each (2 CW triangles)
    static GLSpriteVertex verts[k_max_quads * 6];
    int v = 0;
    for (const auto& q : m_quads)
    {
        GLSpriteVertex tl{ q.x0, q.y0, q.u0, q.v0, q.r, q.g, q.b, q.a, q.mode, q.z };
        GLSpriteVertex tr{ q.x1, q.y0, q.u1, q.v0, q.r, q.g, q.b, q.a, q.mode, q.z };
        GLSpriteVertex bl{ q.x0, q.y1, q.u0, q.v1, q.r, q.g, q.b, q.a, q.mode, q.z };
        GLSpriteVertex br{ q.x1, q.y1, q.u1, q.v1, q.r, q.g, q.b, q.a, q.mode, q.z };

        verts[v++] = tl; verts[v++] = tr; verts[v++] = br; // tri 1
        verts[v++] = tl; verts[v++] = br; verts[v++] = bl; // tri 2
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(v * sizeof(GLSpriteVertex)), verts);

    const int flush_w = (m_override_w > 0) ? m_override_w : (int)MyScreenWidth;
    const int flush_h = (m_override_h > 0) ? m_override_h : (int)MyScreenHeight;

    // Debug logging for coordinate conversion
    static int flush_debug_count = 0;
    if (flush_debug_count < 3) {
        SYNCLOG("OpenGLSpriteBackend::flush: screen_dims=(%d,%d) override=(%d,%d) inv_screen=(%.4f,%.4f) quads=%d", 
                (int)MyScreenWidth, (int)MyScreenHeight, m_override_w, m_override_h, 
                1.0f / (float)flush_w, 1.0f / (float)flush_h, (int)m_quads.size());
        if (!m_quads.empty()) {
            const auto& first = m_quads[0];
            SYNCLOG("  First quad: pos=(%.1f,%.1f)-(%.1f,%.1f) z=%.3f", 
                    first.x0, first.y0, first.x1, first.y1, first.z);
        }
        flush_debug_count++;
    }

    glUseProgram(m_shader);
    glUniform2f(m_loc_inv_screen,
                1.0f / (float)flush_w,
                1.0f / (float)flush_h);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlas.GetTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, v);
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glUseProgram(0);

    m_quads.clear();
}

#endif // RENDERER_OPENGL_ENABLED
