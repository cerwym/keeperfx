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
#include "renderer/RendererManager.h"
#include "renderer/opengl/GLTileAtlas.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include "renderer/opengl/GLWorldViewRenderer.h"
#include "renderer/opengl/GLShaderLoader.h"

#include "bflib_video.h"    // lbDisplay, lbPaletteColors, MyScreenWidth/Height
#include "bflib_render.h"   // render_fade_tables
#include "platform.h"       // platform_create_gl_context / swap / destroy
#include "renderer/RenderPass_C.h"
#include "engine_textures.h" // update_animating_texture_maps()

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <cstring>
#include "post_inc.h"

extern "C" { extern float g_palette_possession_tint; }

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
    m_uTintFactor = glGetUniformLocation(m_shader, "u_tint_factor");

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

    // Sprite atlas for UI panel sprites (gui_panel_sprites, button_sprites).
    // Sheets are added in create_ui_renderer() after this returns.
    m_sprite_atlas = new GLSpriteAtlas();
    if (!m_sprite_atlas->Init())
    {
        WARNLOG("RendererOpenGL: UI sprite atlas init failed — panel sprites will use staging buffer");
        delete m_sprite_atlas;
        m_sprite_atlas = nullptr;
    }

    return true;
}

void RendererOpenGL::Shutdown()
{
    delete m_tile_atlas;
    m_tile_atlas = nullptr;

    delete m_sprite_atlas;
    m_sprite_atlas = nullptr;

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
    // Idempotent: multiple LbScreenLock calls per frame must not clear the UI queue again.
    // The staging buffer has the same guard via m_staging_cleared / LockFramebuffer().
    if (m_frame_begun) return true;
    m_frame_begun = true;

    // Lazy-retry resources that depend on game data loaded after Init().
    if (!m_texFade && render_fade_tables)
        init_fade_table_texture();
    if (m_tile_atlas && !m_tile_atlas->IsInitialized())
        init_tile_atlas();

    // Re-upload the animated tile atlas rows only when the game-logic tick has
    // actually advanced the animation (update_animating_texture_maps() called from
    // main.cpp swaps block_ptrs pointers, changing the sentinel value).
    // This avoids expensive palette→RGBA8 decodes + GPU uploads at the render
    // frame rate (60 fps); uploads now happen at the game-tick rate only.
    if (m_tile_atlas && m_tile_atlas->IsInitialized())
    {
        const uint8_t* anim_sentinel = block_ptrs[TEXTURE_BLOCKS_STAT_COUNT_A];
        if (anim_sentinel != m_last_anim_sentinel)
        {
            m_tile_atlas->UpdateAnimatedTiles();
            m_last_anim_sentinel = anim_sentinel;
        }
    }

    RenderPass_BeginFrame();
    UIRenderer_Clear();
    return true;
}

void RendererOpenGL::EndFrame()
{
    // Upload palette unconditionally — it may have changed this frame via LbPaletteSet.
    // Palette switches happen rarely (level load, possession), so the overhead of a
    // 1 KB CPU expand + glTexSubImage1D is negligible compared to other frame work.
    upload_palette_texture();

    // Restore depth mask before clearing — GPUFlushNow() ends with
    // glDepthMask(GL_FALSE) to protect against accidental depth writes during
    // the overlay blit, but glClear(GL_DEPTH_BUFFER_BIT) respects the mask.
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Flush GPU world geometry + depth-correct sprites.
    // Must run BEFORE the staging buffer is uploaded so that any sprite draws
    // that fall back to the CPU blitter (atlas miss) write into m_stagingBuf
    // while lbDisplay.WScreen is temporarily restored to point at it.
    if (m_world_renderer)
        m_world_renderer->GPUFlushNow(m_stagingBuf);

    // Flush layer-0 (back) GPU UI elements — sidebar background panels that must land
    // beneath the CPU staging-buffer blit so that CPU-drawn text, gold digits, and
    // portraits composite on top of them.
    UIRenderer_FlushBack();

    // Upload CPU framebuffer to index texture (AFTER GPUFlushNow so that
    // CPU-path sprite fallbacks written during the sprite replay are included).
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_stagingW, m_stagingH, GL_RED, GL_UNSIGNED_BYTE, m_stagingBuf);

    // CPU framebuffer blit — palette index 0 is transparent so the GPU back-layer
    // sidebar panels show through in transparent regions, while non-zero CPU pixels
    // (text, digits, portraits on the sidebar) composite correctly on top.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, m_texPalette);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_shader);
    glUniform1f(m_uTintFactor, g_palette_possession_tint);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    // Flush GPU text — rendered after the staging blit so it appears over WScreen
    // content, but before FlushFront so the front-layer UI sprites sit on top.
    TextRenderer_Flush();

    // Flush layer-1 (front) GPU UI elements — escape menu icons/buttons, minimap,
    // slab selectors, power-hand — on top of the staging blit so they appear over
    // CPU-drawn panel backgrounds rather than underneath them.
    UIRenderer_FlushFront();

    RenderPass_EndFrame();
    platform_swap_gl_buffers(lbWindow);
    m_staging_cleared = false; // next frame's first LockFramebuffer will clear the staging buffer
    m_frame_begun     = false; // allow BeginFrame to run fully on the next frame
}

uint8_t* RendererOpenGL::LockFramebuffer(int* out_pitch)
{
    // Clear once per frame on the first lock so old pixel data doesn't
    // accumulate.  Subsequent locks within the same frame are additive.
    if (!m_staging_cleared && m_stagingBuf)
    {
        memset(m_stagingBuf, 0, m_stagingW * m_stagingH);
        m_staging_cleared = true;
    }
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

IWorldViewRenderer* RendererOpenGL::GetWorldViewRenderer()
{
    return RendererGetWorldViewRenderer();
}

IMapFadePass* RendererOpenGL::GetMapFadePass()
{
    return RendererGetMapFadePass();
}

ITextRenderer* RendererOpenGL::GetTextRenderer()
{
    return RendererGetTextRenderer();
}

IUIRenderer* RendererOpenGL::GetUIRenderer()
{
    return RendererGetUIRenderer();
}

/******************************************************************************/


bool RendererOpenGL::compile_shaders()
{
    std::string vert_src = get_embedded_shader_source("palette_blit_vert.glsl");
    std::string frag_src = get_embedded_shader_source("palette_blit_frag.glsl");
    if (vert_src.empty() || frag_src.empty())
        return false;

    unsigned int vert = compile_shader(GL_VERTEX_SHADER,   vert_src.c_str());
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, frag_src.c_str());
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
    if (!m_tile_atlas)
        m_tile_atlas = new GLTileAtlas();
    return m_tile_atlas->Init();
}

void RendererOpenGL::InvalidateTileAtlas()
{
    if (m_tile_atlas)
        m_tile_atlas->Free();
    // Also reset the anim sentinel so the rebuilt atlas is immediately populated
    // with the animated tile strip on the next BeginFrame().
    m_last_anim_sentinel = nullptr;
}
