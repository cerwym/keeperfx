/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererOpenGL.cpp
 *     OpenGL renderer backend.
 * @par Purpose:
 *     Presents the software-rendered 8-bit paletted framebuffer via OpenGL
 *     3.3 Core.  Uses a two-texture approach: an 8-bit index texture and a
 *     256-entry RGBA palette texture.  The fragment shader does the palette
 *     lookup so the upload is a single byte per pixel.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererOpenGL.h"
#include "renderer/opengl/GLTileAtlas.h"
#include "renderer/opengl/GLWorldViewRenderer.h"

#include "bflib_video.h"    // lbDisplay, lbPaletteColors, MyScreenWidth/Height
#include "bflib_render.h"   // render_fade_tables
#include "platform.h"       // platform_create_gl_context / swap / destroy

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/
// Fullscreen quad: two triangles covering NDC [-1,1]
static const float k_quadVerts[] = {
    // pos (xy)   uv
    -1.f, -1.f,   0.f, 1.f,
     1.f, -1.f,   1.f, 1.f,
     1.f,  1.f,   1.f, 0.f,

    -1.f, -1.f,   0.f, 1.f,
     1.f,  1.f,   1.f, 0.f,
    -1.f,  1.f,   0.f, 0.f,
};

static const char* k_vertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)";

// Fragment shader: sample 8-bit index texture, look up palette.
// Palette index 0 is the transparent key used in the 3D viewport background,
// so GPU world geometry shows through where no CPU-drawn pixels exist.
static const char* k_fragSrc = R"(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_index;    // R8 — 8-bit palette index
uniform sampler1D u_palette;  // RGBA8 — 256-entry palette
void main() {
    float idx = texture(u_index, v_uv).r;
    fragColor  = texture(u_palette, idx);
    // Suppress palette index 0 so GPU tiles show through in the 3D viewport.
    fragColor.a = step(0.5 / 256.0, idx);
}
)";

/******************************************************************************/

static unsigned int compile_shader(GLenum type, const char* src)
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
        ERRORLOG("RendererOpenGL shader compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/

RendererOpenGL::RendererOpenGL() = default;

RendererOpenGL::~RendererOpenGL()
{
    Shutdown();
}

bool RendererOpenGL::Init()
{
    // Create GL context (SDL2-based on desktop; see platform_gl_sdl2.cpp)
    if (!platform_create_gl_context(lbWindow))
    {
        ERRORLOG("RendererOpenGL::Init: failed to create GL context: %s", SDL_GetError());
        return false;
    }

    // Load GL function pointers via glad
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        ERRORLOG("RendererOpenGL::Init: glad failed to load GL function pointers");
        platform_destroy_gl_context();
        return false;
    }

    if (!compile_shaders())
    {
        platform_destroy_gl_context();
        return false;
    }

    // ── Fullscreen palette-blit quad ─────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_quadVerts), k_quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // 8-bit index texture (sized to screen, filled each frame)
    m_stagingW = MyScreenWidth;
    m_stagingH = MyScreenHeight;
    m_stagingBuf = new uint8_t[m_stagingW * m_stagingH]();

    glGenTextures(1, &m_texIndex);
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_stagingW, m_stagingH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    // Palette texture (256 RGBA entries)
    glGenTextures(1, &m_texPalette);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    upload_palette_texture();

    // Bind sampler uniforms
    glUseProgram(m_shader);
    glUniform1i(glGetUniformLocation(m_shader, "u_index"),   0);
    glUniform1i(glGetUniformLocation(m_shader, "u_palette"), 1);

    // ── World-geometry GPU resources ─────────────────────────────────────────
    if (!init_fade_table_texture())
    {
        WARNLOG("RendererOpenGL: fade table texture init failed — world GPU renderer disabled");
        // Non-fatal: framebuffer blit still works
    }

    if (!init_tile_atlas())
    {
        WARNLOG("RendererOpenGL: tile atlas init failed — world GPU renderer disabled");
    }

    return true;
}

void RendererOpenGL::Shutdown()
{
    delete m_tile_atlas;
    m_tile_atlas = nullptr;

    delete[] m_stagingBuf;
    m_stagingBuf = nullptr;

    if (m_vao)     { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo);        m_vbo = 0; }
    if (m_shader)  { glDeleteProgram(m_shader);          m_shader = 0; }
    if (m_texIndex)   { glDeleteTextures(1, &m_texIndex);   m_texIndex = 0; }
    if (m_texPalette) { glDeleteTextures(1, &m_texPalette); m_texPalette = 0; }
    if (m_texFade)    { glDeleteTextures(1, &m_texFade);    m_texFade = 0; }

    platform_destroy_gl_context();
}

bool RendererOpenGL::BeginFrame()
{
    return true;
}

void RendererOpenGL::EndFrame()
{
    // Upload palette (may have changed this frame via LbPaletteSet)
    upload_palette_texture();

    // Upload CPU framebuffer to index texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_stagingW, m_stagingH, GL_RED, GL_UNSIGNED_BYTE, m_stagingBuf);

    glClear(GL_COLOR_BUFFER_BIT);

    // Flush GPU world geometry first (tiles rendered beneath CPU overlay)
    if (m_world_renderer)
        m_world_renderer->GPUFlushNow();

    // CPU framebuffer blit ON TOP — creatures, UI, sprites.
    // Palette index 0 is transparent so GPU tiles show through in the 3D
    // viewport where the CPU staging buffer was zeroed by BeginWorldPass.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_shader);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    platform_swap_gl_buffers(lbWindow);
}

uint8_t* RendererOpenGL::LockFramebuffer(int* out_pitch)
{
    if (out_pitch)
        *out_pitch = m_stagingW;
    return m_stagingBuf;
}

void RendererOpenGL::UnlockFramebuffer()
{
    // Nothing to do — CPU writes go directly into m_stagingBuf.
}

const char* RendererOpenGL::GetName() const
{
    return "OpenGL";
}

bool RendererOpenGL::SupportsRuntimeSwitch() const
{
    return true;
}

/******************************************************************************/

bool RendererOpenGL::compile_shaders()
{
    unsigned int vert = compile_shader(GL_VERTEX_SHADER,   k_vertSrc);
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, k_fragSrc);
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
        ERRORLOG("RendererOpenGL shader link error: %s", log);
        glDeleteProgram(m_shader);
        m_shader = 0;
        return false;
    }
    return true;
}

void RendererOpenGL::upload_palette_texture()
{
    // lbPalette is unsigned char[768] (R, G, B per entry, 6-bit values)
    uint8_t rgba[256 * 4];
    for (int i = 0; i < 256; ++i)
    {
        rgba[i * 4 + 0] = (uint8_t)(lbPalette[i * 3 + 0] << 2);
        rgba[i * 4 + 1] = (uint8_t)(lbPalette[i * 3 + 1] << 2);
        rgba[i * 4 + 2] = (uint8_t)(lbPalette[i * 3 + 2] << 2);
        rgba[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

bool RendererOpenGL::init_fade_table_texture()
{
    if (!render_fade_tables)
    {
        WARNLOG("RendererOpenGL::init_fade_table_texture — render_fade_tables not ready");
        return false;
    }

    glGenTextures(1, &m_texFade);
    glBindTexture(GL_TEXTURE_2D, m_texFade);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // 256 palette indices × 256 shade levels = 65536 bytes
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0,
                 GL_RED, GL_UNSIGNED_BYTE, render_fade_tables);
    return true;
}

bool RendererOpenGL::init_tile_atlas()
{
    m_tile_atlas = new GLTileAtlas();
    if (!m_tile_atlas->Init())
    {
        delete m_tile_atlas;
        m_tile_atlas = nullptr;
        return false;
    }
    return true;
}
