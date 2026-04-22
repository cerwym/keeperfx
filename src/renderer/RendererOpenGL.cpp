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
#include "renderer/opengl/GLUIRenderer.h"
#include "renderer/opengl/GLShaderLoader.h"
#include "kfx/profiling/KfxProfiling.h"

#include "bflib_video.h"    // lbDisplay, lbPaletteColors, MyScreenWidth/Height
#include "bflib_render.h"   // render_fade_tables
#include "bflib_vidraw.h"   // vec_window_width/height (PiP projection override)
#include "platform.h"       // platform_create_gl_context / swap / destroy
#include "renderer/RenderPass_C.h"
#include "engine_textures.h" // update_animating_texture_maps()
#include "engine_render.h"   // draw_view()
#include "engine_redraw.h"   // setup_engine_window / store_engine_window (PiP viewport)

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

    // Initialise Tracy GPU profiling context (requires GL function pointers).
    // Skip when RenderDoc is injected: TracyGpuContext allocates 65536 GL timer
    // queries that RenderDoc's per-frame state serialisation enumerates.  When
    // RenderDoc then captures or tracks those query objects its internal tables
    // can overflow or corrupt Tracy's query pool, producing a blank C++ exception
    // (Access Violation surfaced through RenderDoc's VEH) in TracyGpuCollect the
    // first time the ring-buffer wraps — typically at the heartzoom→dungeon
    // transition after ~16 frames.  Skipping the create leaves GetGpuCtx().ptr
    // as nullptr; KFX_GPU_COLLECT and KFX_GPU_ZONE both guard against that.
    if (!platform_is_renderdoc_present()) {
        KFX_GPU_CTX_CREATE();
    } else {
        SYNCLOG("RenderDoc detected — Tracy GPU profiling disabled to avoid timer-query conflict");
    }

    if (!compile_shaders())
    {
        platform_destroy_gl_context();
        return false;
    }

    // ── Fullscreen palette-blit quad ─────────────────────────────────────────
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_vao, "Blit/QuadVAO");
    KFX_GL_LABEL(GL_BUFFER, m_vbo, "Blit/QuadVBO");
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(k_quadVerts), k_quadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Screen dimensions (used throughout EndFrame for viewport sizing).
    m_screenW = MyScreenWidth;
    m_screenH = MyScreenHeight;

    // Write-discard buffer: returned by LockFramebuffer() so LbScreenLock() succeeds.
    // Content is never uploaded to the GPU. Allocation is zero-initialised.
    m_discardBuf = new uint8_t[(size_t)m_screenW * (size_t)m_screenH]();
    m_discard_cleared = true;

    // Transparent overlay texture — GL_R8, screen-sized.
    // SubmitTransparentBlit() uploads directly into this texture; EndFrame composites
    // it over the GPU frame with index-0 transparency (for landview window frame etc.).
    glGenTextures(1, &m_texIndex);
    KFX_GL_LABEL(GL_TEXTURE, m_texIndex, "Blit/TransparentOverlayTex");
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_screenW, m_screenH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    // Palette texture (256×1 RGBA8 — shared across all subsystems)
    glGenTextures(1, &m_texPalette);
    KFX_GL_LABEL(GL_TEXTURE, m_texPalette, "Blit/PaletteTex");
    glBindTexture(GL_TEXTURE_2D, m_texPalette);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    upload_palette_texture();

    // Bind sampler uniforms once — used by all transparent/palette-decoded blit paths.
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

    // ── Raw-image GPU blit (frontend background images) ────────────────────────
    // Compile shader: reuse palette_blit_vert.glsl + rawimage_blit_frag.glsl.
    // Fatal if shader compilation fails — no CPU fallback is permitted in GL mode.
    {
        std::string rv_src = get_embedded_shader_source("palette_blit_vert.glsl");
        std::string rf_src = get_embedded_shader_source("rawimage_blit_frag.glsl");
        if (rv_src.empty() || rf_src.empty())
        {
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader source missing");
            platform_destroy_gl_context();
            return false;
        }
        unsigned int rv = compile_shader(GL_VERTEX_SHADER,   rv_src.c_str());
        unsigned int rf = compile_shader(GL_FRAGMENT_SHADER, rf_src.c_str());
        if (!rv || !rf)
        {
            if (rv) glDeleteShader(rv);
            if (rf) glDeleteShader(rf);
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader compile failed");
            platform_destroy_gl_context();
            return false;
        }
        m_rawblit_shader = glCreateProgram();
        glAttachShader(m_rawblit_shader, rv);
        glAttachShader(m_rawblit_shader, rf);
        glLinkProgram(m_rawblit_shader);
        glDeleteShader(rv);
        glDeleteShader(rf);
        int ok = 0;
        glGetProgramiv(m_rawblit_shader, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetProgramInfoLog(m_rawblit_shader, sizeof(log), nullptr, log);
            ERRORLOG("RendererOpenGL::Init: rawimage blit shader link error: %s", log);
            glDeleteProgram(m_rawblit_shader);
            m_rawblit_shader = 0;
            platform_destroy_gl_context();
            return false;
        }
        glUseProgram(m_rawblit_shader);
        glUniform1i(glGetUniformLocation(m_rawblit_shader, "u_index"),   0);
        glUniform1i(glGetUniformLocation(m_rawblit_shader, "u_palette"), 1);
        KFX_GL_LABEL(GL_PROGRAM, m_rawblit_shader, "RawBlit/Program");
    }

    // ── Overhead map tile colour shader ────────────────────────────────────────
    // Same vertex shader as rawblit, but the fragment shader discards palette
    // index 0 so that unrevealed tiles are transparent (parchment shows through).
    // Blending is enabled for the overhead map pass.
    {
        std::string rv_src = get_embedded_shader_source("palette_blit_vert.glsl");
        std::string rf_src = get_embedded_shader_source("overhead_map_frag.glsl");
        if (!rv_src.empty() && !rf_src.empty())
        {
            unsigned int rv = compile_shader(GL_VERTEX_SHADER,   rv_src.c_str());
            unsigned int rf = compile_shader(GL_FRAGMENT_SHADER, rf_src.c_str());
            if (rv && rf)
            {
                m_overhead_map_shader = glCreateProgram();
                glAttachShader(m_overhead_map_shader, rv);
                glAttachShader(m_overhead_map_shader, rf);
                glLinkProgram(m_overhead_map_shader);
                glDeleteShader(rv);
                glDeleteShader(rf);
                int ok = 0;
                glGetProgramiv(m_overhead_map_shader, GL_LINK_STATUS, &ok);
                if (ok)
                {
                    glUseProgram(m_overhead_map_shader);
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_index"),   0);
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_palette"), 1);
                    KFX_GL_LABEL(GL_PROGRAM, m_overhead_map_shader, "OverheadMap/Program");
                }
                else
                {
                    char log[512];
                    glGetProgramInfoLog(m_overhead_map_shader, sizeof(log), nullptr, log);
                    WARNLOG("RendererOpenGL::Init: overhead map shader link error: %s", log);
                    glDeleteProgram(m_overhead_map_shader);
                    m_overhead_map_shader = 0;
                }
            }
            else
            {
                if (rv) glDeleteShader(rv);
                if (rf) glDeleteShader(rf);
                WARNLOG("RendererOpenGL::Init: overhead map shader compile failed — falling back to rawblit");
            }
        }
    }

    // ── Zoom-box tile shader ───────────────────────────────────────────────────
    // Samples the R8 tile atlas per quad UV and performs a palette lookup.
    // Index 0 is discarded so transparent tile pixels let the parchment show
    // through.  Non-fatal: falls back to no-draw if compile fails.
    {
        std::string rv_src = get_embedded_shader_source("palette_blit_vert.glsl");
        std::string rf_src = get_embedded_shader_source("zoom_tile_frag.glsl");
        if (!rv_src.empty() && !rf_src.empty())
        {
            unsigned int rv = compile_shader(GL_VERTEX_SHADER,   rv_src.c_str());
            unsigned int rf = compile_shader(GL_FRAGMENT_SHADER, rf_src.c_str());
            if (rv && rf)
            {
                m_zoom_tile_shader = glCreateProgram();
                glAttachShader(m_zoom_tile_shader, rv);
                glAttachShader(m_zoom_tile_shader, rf);
                glLinkProgram(m_zoom_tile_shader);
                glDeleteShader(rv);
                glDeleteShader(rf);
                int ok = 0;
                glGetProgramiv(m_zoom_tile_shader, GL_LINK_STATUS, &ok);
                if (ok)
                {
                    glUseProgram(m_zoom_tile_shader);
                    glUniform1i(glGetUniformLocation(m_zoom_tile_shader, "u_index"),   0);
                    glUniform1i(glGetUniformLocation(m_zoom_tile_shader, "u_palette"), 1);
                    KFX_GL_LABEL(GL_PROGRAM, m_zoom_tile_shader, "ZoomTile/Program");
                }
                else
                {
                    char log[512];
                    glGetProgramInfoLog(m_zoom_tile_shader, sizeof(log), nullptr, log);
                    WARNLOG("RendererOpenGL::Init: zoom tile shader link error: %s", log);
                    glDeleteProgram(m_zoom_tile_shader);
                    m_zoom_tile_shader = 0;
                }
            }
            else
            {
                if (rv) glDeleteShader(rv);
                if (rf) glDeleteShader(rf);
                WARNLOG("RendererOpenGL::Init: zoom tile shader compile failed");
            }
        }
    }

    // Raw-blit quad VAO/VBO — positions/UVs updated per blit in EndFrame().
    glGenVertexArrays(1, &m_rawblit_vao);
    glGenBuffers(1, &m_rawblit_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_rawblit_vao, "RawBlit/QuadVAO");
    KFX_GL_LABEL(GL_BUFFER, m_rawblit_vbo, "RawBlit/QuadVBO");
    glBindVertexArray(m_rawblit_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Raw-blit index texture — uploads source image data per blit.
    glGenTextures(1, &m_rawblit_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_rawblit_tex, "RawBlit/IndexTex");
    glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Overhead map tile colour texture — GL_R8, tiles_x × tiles_y (typically 85×85).
    // Lazily resized on first use; drawn as an opaque rect quad over the parchment bg.
    glGenTextures(1, &m_overhead_map_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_overhead_map_tex, "OverheadMap/IndexTex");
    glBindTexture(GL_TEXTURE_2D, m_overhead_map_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── FMV video-frame GPU blit — reuses rawblit shader; own VAO/VBO/textures. ──
    glGenVertexArrays(1, &m_fmv_vao);
    glGenBuffers(1, &m_fmv_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_fmv_vao, "FmvBlit/QuadVAO");
    KFX_GL_LABEL(GL_BUFFER, m_fmv_vbo, "FmvBlit/QuadVBO");
    glBindVertexArray(m_fmv_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_fmv_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Per-frame pixel index texture (R8, GL_NEAREST — palette indices must not interpolate).
    glGenTextures(1, &m_fmv_index_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_fmv_index_tex, "FmvBlit/IndexTex");
    glBindTexture(GL_TEXTURE_2D, m_fmv_index_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Per-video palette texture (256×1 RGBA8; GL_BGRA upload swaps B/R automatically).
    glGenTextures(1, &m_fmv_palette_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_fmv_palette_tex, "FmvBlit/PaletteTex");
    glBindTexture(GL_TEXTURE_2D, m_fmv_palette_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Landview zoom GPU blit (Phase D campaign-map zoom transition) ──────────
    // Compile shader: reuse palette_blit_vert.glsl + landview_zoom_frag.glsl.
    // The zoom fragment shader uses gl_FragCoord to compute source UVs from
    // per-frame uniforms (zoom centre in map & screen coords, scale).
    {
        std::string zv_src = get_embedded_shader_source("palette_blit_vert.glsl");
        std::string zf_src = get_embedded_shader_source("landview_zoom_frag.glsl");
        if (zv_src.empty() || zf_src.empty())
        {
            ERRORLOG("RendererOpenGL::Init: landview zoom shader source missing");
            platform_destroy_gl_context();
            return false;
        }
        unsigned int zv = compile_shader(GL_VERTEX_SHADER,   zv_src.c_str());
        unsigned int zf = compile_shader(GL_FRAGMENT_SHADER, zf_src.c_str());
        if (!zv || !zf)
        {
            if (zv) glDeleteShader(zv);
            if (zf) glDeleteShader(zf);
            ERRORLOG("RendererOpenGL::Init: landview zoom shader compile failed");
            platform_destroy_gl_context();
            return false;
        }
        m_zoom_shader = glCreateProgram();
        glAttachShader(m_zoom_shader, zv);
        glAttachShader(m_zoom_shader, zf);
        glLinkProgram(m_zoom_shader);
        glDeleteShader(zv);
        glDeleteShader(zf);
        int ok = 0;
        glGetProgramiv(m_zoom_shader, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetProgramInfoLog(m_zoom_shader, sizeof(log), nullptr, log);
            ERRORLOG("RendererOpenGL::Init: landview zoom shader link error: %s", log);
            glDeleteProgram(m_zoom_shader);
            m_zoom_shader = 0;
            platform_destroy_gl_context();
            return false;
        }
        glUseProgram(m_zoom_shader);
        glUniform1i(glGetUniformLocation(m_zoom_shader, "u_index"),   0);
        glUniform1i(glGetUniformLocation(m_zoom_shader, "u_palette"), 1);
        m_zoom_u_center_map = glGetUniformLocation(m_zoom_shader, "u_center_map");
        m_zoom_u_screen_ctr = glGetUniformLocation(m_zoom_shader, "u_screen_center");
        m_zoom_u_scale      = glGetUniformLocation(m_zoom_shader, "u_scale");
        m_zoom_u_inv_map_sz = glGetUniformLocation(m_zoom_shader, "u_inv_map_size");
        m_zoom_u_screen_h   = glGetUniformLocation(m_zoom_shader, "u_screen_h");
        KFX_GL_LABEL(GL_PROGRAM, m_zoom_shader, "LandviewZoom/Program");
    }

    // Landview zoom index texture — R8, 1280×960, GL_NEAREST, CLAMP_TO_EDGE.
    glGenTextures(1, &m_zoom_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_zoom_tex, "LandviewZoom/IndexTex");
    glBindTexture(GL_TEXTURE_2D, m_zoom_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Sprite atlas for UI panel sprites (gui_panel_sprites, button_sprites).
    // Sheets are added in create_ui_renderer() after this returns.
    m_sprite_atlas = new GLSpriteAtlas();
    if (!m_sprite_atlas->Init())
    {
        WARNLOG("RendererOpenGL: UI sprite atlas init failed — panel sprites will use staging buffer");
        delete m_sprite_atlas;
        m_sprite_atlas = nullptr;
    }
    else
    {
        SYNCLOG("RendererOpenGL: UI sprite atlas initialized successfully");
    }

    return true;
}

void RendererOpenGL::Shutdown()
{
    delete m_tile_atlas;
    m_tile_atlas = nullptr;

    delete m_sprite_atlas;
    m_sprite_atlas = nullptr;

    delete[] m_discardBuf;
    m_discardBuf = nullptr;

    if (m_vao)     { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo);        m_vbo = 0; }
    if (m_shader)  { glDeleteProgram(m_shader);          m_shader = 0; }
    if (m_tintProg){ glDeleteProgram(m_tintProg);        m_tintProg = 0; }
    if (m_texIndex)   { glDeleteTextures(1, &m_texIndex);   m_texIndex = 0; }
    if (m_texPalette) { glDeleteTextures(1, &m_texPalette); m_texPalette = 0; }
    if (m_texFade)    { glDeleteTextures(1, &m_texFade);    m_texFade = 0; }
    if (m_rawblit_shader)       { glDeleteProgram(m_rawblit_shader);            m_rawblit_shader = 0; }
    if (m_overhead_map_shader)  { glDeleteProgram(m_overhead_map_shader);       m_overhead_map_shader = 0; }
    if (m_zoom_tile_shader)     { glDeleteProgram(m_zoom_tile_shader);          m_zoom_tile_shader = 0; }
    if (m_rawblit_vao)    { glDeleteVertexArrays(1, &m_rawblit_vao);      m_rawblit_vao = 0; }
    if (m_rawblit_vbo)    { glDeleteBuffers(1, &m_rawblit_vbo);           m_rawblit_vbo = 0; }
    if (m_rawblit_tex)    { glDeleteTextures(1, &m_rawblit_tex);          m_rawblit_tex = 0; }
    if (m_overhead_map_tex) { glDeleteTextures(1, &m_overhead_map_tex);   m_overhead_map_tex = 0; }
    if (m_fmv_vao)          { glDeleteVertexArrays(1, &m_fmv_vao);        m_fmv_vao = 0; }
    if (m_fmv_vbo)          { glDeleteBuffers(1, &m_fmv_vbo);             m_fmv_vbo = 0; }
    if (m_fmv_index_tex)    { glDeleteTextures(1, &m_fmv_index_tex);      m_fmv_index_tex = 0; }
    if (m_fmv_palette_tex)  { glDeleteTextures(1, &m_fmv_palette_tex);    m_fmv_palette_tex = 0; }
    if (m_zoom_shader)      { glDeleteProgram(m_zoom_shader);              m_zoom_shader = 0; }
    if (m_zoom_tex)         { glDeleteTextures(1, &m_zoom_tex);            m_zoom_tex = 0; }
    for (PiPFBO& slot : m_pip_fbos)
    {
        if (slot.fbo)       { glDeleteFramebuffers(1,   &slot.fbo);       slot.fbo       = 0; }
        if (slot.color_tex) { glDeleteTextures(1,        &slot.color_tex); slot.color_tex = 0; }
        if (slot.depth_rb)  { glDeleteRenderbuffers(1,  &slot.depth_rb);  slot.depth_rb  = 0; }
    }
    m_pip_fbos.clear();

    platform_destroy_gl_context();
}

void RendererOpenGL::ClearScreen(uint8_t colour_index)
{
    m_clearColourIndex = colour_index;
}

bool RendererOpenGL::BeginFrame()
{
    // Idempotent: multiple LbScreenLock calls per frame must not clear the UI queue again.
    if (m_frame_begun) return true;
    m_frame_begun = true;

    // Lazy-retry resources that depend on game data loaded after Init().
    if (!m_texFade && render_fade_tables)
        init_fade_table_texture();
    if (m_tile_atlas && !m_tile_atlas->IsInitialized())
        init_tile_atlas();

    if (m_tile_atlas && m_tile_atlas->IsInitialized())
    {
        GLWorldViewRenderer* glwr = dynamic_cast<GLWorldViewRenderer*>(m_world_renderer);
        if (glwr)
            glwr->TryEarlyInit();
    }

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
    CursorLayer_Clear();
    return true;
}

/** ============================================================
 *  Frame composition pipeline (bottom-to-top draw order)
 *  ============================================================
 *
 *  Step 1 — glClear()
 *       Wipes the colour + depth buffer to the GL clear colour (black).
 *       Everything below composites on top of this.
 *
 *  Step 2 — GPUFlushNow()                      [GLWorldViewRenderer]
 *       Replays the bucket-walk command list:
 *         CMD_TILES    — indexed-colour tile meshes (world geometry)
 *         CMD_SHADOWS  — per-creature floor-shadow quads
 *         CMD_SPRITES  — depth-sorted 3D billboard sprites (creatures, objects)
 *         CMD_WORLDTEXT — floating 3D text (gold numbers above piles)
 *       Hardware depth test keeps geometry correctly occluded.
 *       Shade is per-vertex Gouraud from the DK fade table (mode 0) or
 *       per-fragment lightmap (mode 1, Phase 3+).
 *
 *  Step 3 — UIRenderer_FlushBack()               [GLUIRenderer, layer=0]
 *       Flushes UIQuads tagged layer=0 (back):
 *         mode 10 — tiled slab-background quads (sidebar panel fill)
 *         mode  3 — solid-colour quads (progress bar trough fills)
 *         mode  0 — palette-indexed atlas sprites (back-layer panel sprites)
 *       Must land BEFORE the staging blit (Step 4) so that CPU-drawn text
 *       from the staging buffer composites on top of panel backgrounds.
 *       (GPU-active path skips Step 4, but the ordering still matters for
 *       slab backgrounds vs. front-layer sprites.)
 *
 *  Step 4 — Transparent overlay blit              [only when SubmitTransparentBlit called]
 *       Source data already uploaded to m_texIndex inside SubmitTransparentBlit().
 *       Draws a fullscreen quad through the palette-lookup shader.
 *       Index 0 = transparent (alpha 0); non-zero pixels composite over Step 3.
 *       Used by compressed_window_draw() (campaign map window frame) and similar.
 *
 *  Step 5 — TextRenderer_Flush()                 [GLTextRenderer, non-overlay]
 *       Renders all non-overlay deferred text BEFORE sprite layers so the
 *       tooltip box (layer 3) can composite on top of event messages etc.
 *
 *  Step 6 — UIRenderer_FlushFront()              [GLUIRenderer, layers 1→2→3]
 *       Layer 1: panel/button atlas sprites, escape menu, battler icons, gems.
 *       Layer 2: world-depth-tested sprites (creature status flowers, payday digits).
 *       Layer 3: top-overlay sprites (tooltip box, slab selector) — rendered last.
 *
 *  Step 7 — TextRenderer_FlushOverlay()          [GLTextRenderer, overlay]
 *       Renders overlay-tagged text (tooltip text) AFTER layer-3 sprites so it
 *       is readable over the tooltip box background.
 *
 *  Step 8 — Screen-tint overlay                  [RendererOpenGL, fullscreen quad]
 *       Composites possession/pain (red) or death-flash (white) tint over all
 *       scene content.  Runs before the cursor so the cursor is never tinted.
 *
 *  Step 9 — CursorLayer_Flush()               [GLCursorLayer]
 *       OS pointer sprite (pointer_sprites atlas) + power-hand keeper sprites,
 *       rendered as absolute last layer before the buffer swap.
 *       Cursor drawn absolutely last — after the tint overlay — so it is always
 *       on top of every other rendered layer.
 *
 *  Step 9 — platform_swap_gl_buffers()
 *       Flips the back buffer to the display.
 *
 *  The GPUFlushNow tile meshes are one draw per tile variation (all-opaque);
 *  shadows are one draw call each; sprites are batched by bucket depth.
 *  UI quads are one draw call per render-mode pass per layer.
 *  ============================================================ */
void RendererOpenGL::EndFrame()
{
    // ── Flicker diagnostic: GL error audit ──────────────────────────────────
    // Check for accumulated GL errors from previous frame's deferred operations.
    // A GL error set by any call silently causes subsequent operations to fail
    // (e.g. textures fail to bind, draw calls produce nothing).  Drain them.
    {
        GLenum err;
        while ((err = glGetError()) != GL_NO_ERROR)
        {
            static int s_glerr_count = 0;
            if (++s_glerr_count <= 50)
                SYNCLOG("FLICKER-DIAG: GL error 0x%X at start of EndFrame (count=%d)", err, s_glerr_count);
        }
    }

    // Upload palette unconditionally — it may have changed this frame via LbPaletteSet.
    // Palette switches happen rarely (level load, possession), so the overhead of a
    // 1 KB CPU expand + glTexSubImage2D is negligible compared to other frame work.
    upload_palette_texture();

    // Restore depth mask before clearing — GPUFlushNow() ends with
    // glDepthMask(GL_FALSE) to protect against accidental depth writes during
    // the overlay blit, but glClear(GL_DEPTH_BUFFER_BIT) respects the mask.
    glDepthMask(GL_TRUE);
    {
        // Resolve palette index → RGBA for the GL clear colour.
        // lbPalette entries are 6-bit (0-63); shift left 2 to get 8-bit (0-252).
        const float r = (float)(lbPalette[m_clearColourIndex * 3 + 0] << 2) / 255.0f;
        const float g = (float)(lbPalette[m_clearColourIndex * 3 + 1] << 2) / 255.0f;
        const float b = (float)(lbPalette[m_clearColourIndex * 3 + 2] << 2) / 255.0f;
        glClearColor(r, g, b, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_clearColourIndex = 0; // reset to black; caller must call ClearScreen() each frame if non-zero

    // Flush GPU world geometry + depth-correct sprites.
    // Runs BEFORE the staging buffer upload so both layers composite correctly.
    if (m_world_renderer)
    {
        m_world_renderer->GPUFlushNow();
        m_rawblit_cached = false;  // gameplay frame — don't replay frontend background
    }
    // GL error check after world pass
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            static int s_cnt = 0;
            if (++s_cnt <= 20) SYNCLOG("FLICKER-DIAG: GL error 0x%X after GPUFlushNow (%d)", err, s_cnt);
        }
    }

    // Flush layer-0 (back) GPU UI elements — sidebar background panels.
    UIRenderer_FlushBack();

    // Raw-image GPU blit — frontend background images (legal, loading, menu bg,
    // map bg, torture, etc.).  Queued by BlitRaw8GPU() during the frame; drawn
    // here as an opaque quad so that the staging-blit overlay (which composites
    // any CPU-drawn menu sprites above index 0) lands on top.
    // During blocking palette-fade loops (ProperFadePalette / fade_in / fade_out),
    // the game calls LbScreenSwap repeatedly without issuing any new draw commands.
    // Re-issue the last frontend rawblit with the freshly-uploaded (darkened) palette
    // so the fade is visible in GPU mode.  Only kicks in when nothing new was queued.
    if (!m_rawblit_pending && m_rawblit_cached)
        m_rawblit_pending = true, m_rawblit_cmd = m_rawblit_cached_cmd;

    if (m_rawblit_pending)
    {
        const RawBlitCmd& cmd = m_rawblit_cmd;

        // Ensure full-screen viewport and no depth interaction.
        // UIRenderer_FlushBack() returns early (no quads) on pure-frontend frames
        // without disabling depth test, so we must guard here explicitly.
        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Upload source image to the raw-blit index texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
        if (cmd.src_w != m_rawblit_tex_w || cmd.src_h != m_rawblit_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cmd.src_w, cmd.src_h,
                         0, GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
            m_rawblit_tex_w = cmd.src_w;
            m_rawblit_tex_h = cmd.src_h;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.src_w, cmd.src_h,
                            GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
        }

        // Palette texture already on unit 1 from upload_palette_texture() above.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        // Build a rect quad in NDC covering [dst_x..dst_x+dst_w] x [dst_y..dst_y+dst_h].
        // Screen-space: y increases downward; NDC: y increases upward.
        const float sw = (float)m_screenW;
        const float sh = (float)m_screenH;
        const float x0 = (float)cmd.dst_x             / sw * 2.0f - 1.0f;
        const float x1 = (float)(cmd.dst_x + cmd.dst_w) / sw * 2.0f - 1.0f;
        const float y0 = 1.0f - (float)cmd.dst_y             / sh * 2.0f;  // top NDC
        const float y1 = 1.0f - (float)(cmd.dst_y + cmd.dst_h) / sh * 2.0f; // bottom NDC

        // Two triangles; UV (0,0) = top-left, (1,1) = bottom-right.
        const float verts[6][4] = {
            { x0, y1,  0.f, 1.f },  // bottom-left
            { x1, y1,  1.f, 1.f },  // bottom-right
            { x1, y0,  1.f, 0.f },  // top-right
            { x0, y1,  0.f, 1.f },  // bottom-left
            { x1, y0,  1.f, 0.f },  // top-right
            { x0, y0,  0.f, 0.f },  // top-left
        };
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Restore depth mask and reset active texture unit to 0 so the
        // subsequent staging blit and other passes start from a clean state.
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);

        m_rawblit_pending = false;
    }

    // Overhead map tile colour GPU blit — drawn after the parchment background
    // rawblit and before the staging overlay so tile colours sit below CPU sprites
    // (room icons, creatures, call-to-arms circles).  Uses the same rawblit shader
    // (palette_blit_vert + rawimage_blit_frag — opaque) and VAO/VBO layout, scaled
    // to the map_area dest rect supplied by draw_overhead_map().
    // Multiple commands allowed per frame (e.g. full map + zoom box).
    for (const OverheadMapCmd& cmd : m_overhead_map_cmds)
    {
        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_overhead_map_tex);
        // Tile rows are tightly packed (1 byte each).  Map width is arbitrary
        // and not guaranteed to be a multiple of 4, so override GL's default
        // 4-byte row alignment to avoid row-start drift.
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (cmd.tiles_x != m_overhead_map_tex_w || cmd.tiles_y != m_overhead_map_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cmd.tiles_x, cmd.tiles_y,
                         0, GL_RED, GL_UNSIGNED_BYTE, cmd.pixels.data());
            m_overhead_map_tex_w = cmd.tiles_x;
            m_overhead_map_tex_h = cmd.tiles_y;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.tiles_x, cmd.tiles_y,
                            GL_RED, GL_UNSIGNED_BYTE, cmd.pixels.data());
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        const float sw = (float)m_screenW;
        const float sh = (float)m_screenH;
        const float x0 = (float)cmd.dst_x               / sw * 2.0f - 1.0f;
        const float x1 = (float)(cmd.dst_x + cmd.dst_w) / sw * 2.0f - 1.0f;
        const float y0 = 1.0f - (float)cmd.dst_y               / sh * 2.0f;
        const float y1 = 1.0f - (float)(cmd.dst_y + cmd.dst_h) / sh * 2.0f;
        const float verts[6][4] = {
            { x0, y1,  0.f, 1.f },
            { x1, y1,  1.f, 1.f },
            { x1, y0,  1.f, 0.f },
            { x0, y1,  0.f, 1.f },
            { x1, y0,  1.f, 0.f },
            { x0, y0,  0.f, 0.f },
        };
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        // Use the overhead-map shader (discards index 0 = unrevealed tiles) so
        // the parchment background shows through unrevealed/undug areas.
        GLuint prog = m_overhead_map_shader ? m_overhead_map_shader : m_rawblit_shader;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    m_overhead_map_cmds.clear();

    // ── Zoom-box tile quads (ZBM_OVERHEAD with actual tile textures) ───────
    // Queued by SubmitZoomBoxTiles(); each entry is one tile quad with UV into
    // the tile atlas.  Drawn on top of the overhead map.
    // Step 1: fill each zoom box region with solid black so unrevealed tiles
    //         and skipped rock tiles appear black rather than showing whatever
    //         is underneath (overhead map, parchment).
    if (!m_zoom_box_bg_cmds.empty())
    {
        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        // Draw a solid-black opaque quad for each zoom-box slot using the
        // screen-tint shader (flat colour, no texture).  This is more robust
        // than glClear+scissor because it goes through the same render pipeline
        // as every other 2-D draw (no scissor state to manage, no glClearColor
        // side-effects, pixel-perfect NDC coverage guaranteed).
        if (m_tintProg)
        {
            static const float k_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            glUseProgram(m_tintProg);
            glUniform4fv(m_uTintColor, 1, k_black);
            glBindVertexArray(m_rawblit_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
            const float sw = (float)m_screenW;
            const float sh = (float)m_screenH;
            for (const ZoomBoxBgCmd& bg : m_zoom_box_bg_cmds)
            {
                const float x0 = (float)bg.x           / sw * 2.0f - 1.0f;
                const float x1 = (float)(bg.x + bg.w)  / sw * 2.0f - 1.0f;
                const float y0 = 1.0f - (float)bg.y           / sh * 2.0f;
                const float y1 = 1.0f - (float)(bg.y + bg.h)  / sh * 2.0f;
                // Two triangles; UV ignored by tint shader (location 1 unused).
                const float verts[6][4] = {
                    { x0, y1,  0.f, 0.f },
                    { x1, y1,  0.f, 0.f },
                    { x1, y0,  0.f, 0.f },
                    { x0, y1,  0.f, 0.f },
                    { x1, y0,  0.f, 0.f },
                    { x0, y0,  0.f, 0.f },
                };
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glBindVertexArray(0);
        }
        else
        {
            // Fallback for the rare case m_tintProg is unavailable.
            float saved_cc[4];
            glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_cc);
            glClearColor(KFX_GL_CLEAR_COLOR);
            glEnable(GL_SCISSOR_TEST);
            for (const ZoomBoxBgCmd& bg : m_zoom_box_bg_cmds)
            {
                int gl_y = m_screenH - (bg.y + bg.h);
                glScissor(bg.x, gl_y, bg.w, bg.h);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
            glClearColor(saved_cc[0], saved_cc[1], saved_cc[2], saved_cc[3]);
        }
        glDepthMask(GL_TRUE);
    }
    m_zoom_box_bg_cmds.clear();

    // Step 2: draw textured tile quads on top of the black background.
    if (!m_zoom_tile_cmds.empty() && m_zoom_tile_shader && m_tile_atlas)
    {
        GLuint atlas_tex = m_tile_atlas->GetAtlasTexture(0);
        if (atlas_tex)
        {
            glViewport(0, 0, m_screenW, m_screenH);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlas_tex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_texPalette);

            glUseProgram(m_zoom_tile_shader);
            glBindVertexArray(m_rawblit_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);

            const float sw = (float)m_screenW;
            const float sh = (float)m_screenH;

            for (const ZoomTileCmd& c : m_zoom_tile_cmds)
            {
                const float x0 = c.dst_x               / sw * 2.0f - 1.0f;
                const float x1 = (c.dst_x + c.dst_w)   / sw * 2.0f - 1.0f;
                const float y0 = 1.0f - c.dst_y               / sh * 2.0f;
                const float y1 = 1.0f - (c.dst_y + c.dst_h)   / sh * 2.0f;
                const float verts[6][4] = {
                    { x0, y1,  c.u0, c.v1 },
                    { x1, y1,  c.u1, c.v1 },
                    { x1, y0,  c.u1, c.v0 },
                    { x0, y1,  c.u0, c.v1 },
                    { x1, y0,  c.u1, c.v0 },
                    { x0, y0,  c.u0, c.v0 },
                };
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }

            glBindVertexArray(0);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glActiveTexture(GL_TEXTURE0);
        }
    }
    m_zoom_tile_cmds.clear();

    // ── PiP isometric render (ZBM_ISOMETRIC zoom-box mode) ────────────────
    // Each entry in m_pip_queue is rendered into its own FBO slot (grown on
    // demand) then submitted to GLUIRenderer for compositing.  Queue cleared.
    if (m_world_renderer && !m_pip_queue.empty())
    {
        GLUIRenderer* ui = dynamic_cast<GLUIRenderer*>(RendererGetUIRenderer());

        for (std::size_t qi = 0; qi < m_pip_queue.size(); ++qi)
        {
            const PiPCmd& pcmd = m_pip_queue[qi];
            const int pw = pcmd.w;
            const int ph = pcmd.h;
            if (pw <= 0 || ph <= 0)
                continue;

            ensure_pip_fbo(qi, pw, ph);
            const PiPFBO& fbo = m_pip_fbos[qi];
            if (!fbo.fbo)
                continue;

            SYNCDBG(7, "PiP[%zu]: fbo=%u pos=(%d,%d) size=(%dx%d) cam=(%d,%d,%d)",
                    qi, fbo.fbo, pcmd.x, pcmd.y, pw, ph,
                    (int)pcmd.cam_copy.mappos.x.val,
                    (int)pcmd.cam_copy.mappos.y.val,
                    (int)pcmd.cam_copy.zoom);

            const long saved_vw = vec_window_width;
            const long saved_vh = vec_window_height;
            vec_window_width  = pw;
            vec_window_height = ph;

            TbGraphicsWindow saved_ewnd;
            store_engine_window(&saved_ewnd, 1);
            setup_engine_window(0, 0, pw, ph);

            if (ui)
                ui->BeginPiPSprites();

            m_world_renderer->BeginWorldPass(nullptr, 0, pw, ph, 0, 0);

            // Save projection globals so main-view mouse→world unprojection is unaffected.
            const Offset saved_vert[3] = { vert_offset[0], vert_offset[1], vert_offset[2] };
            const Offset saved_hori[3] = { hori_offset[0], hori_offset[1], hori_offset[2] };
            const long   saved_x_init  = x_init_off;
            const long   saved_y_init  = y_init_off;

            draw_view(const_cast<Camera*>(&pcmd.cam_copy), 0);

            vert_offset[0] = saved_vert[0]; vert_offset[1] = saved_vert[1]; vert_offset[2] = saved_vert[2];
            hori_offset[0] = saved_hori[0]; hori_offset[1] = saved_hori[1]; hori_offset[2] = saved_hori[2];
            x_init_off = saved_x_init;
            y_init_off = saved_y_init;

            setup_engine_window(saved_ewnd.x, saved_ewnd.y,
                                saved_ewnd.width, saved_ewnd.height);
            vec_window_width  = saved_vw;
            vec_window_height = saved_vh;

            GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
            glViewport(0, 0, pw, ph);
            glClearColor(KFX_GL_CLEAR_COLOR);
            glDepthMask(GL_TRUE);  // GPUFlushNow_ToFBO leaves mask false; ensure depth is cleared
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            m_world_renderer->GPUFlushNow_ToFBO(pw, ph);
            if (ui)
                ui->FlushPiPSprites(pw, ph);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_screenW, m_screenH);
            if (scissor_was_enabled)
                glEnable(GL_SCISSOR_TEST);

            if (ui)
                ui->SubmitFBOQuad(pcmd.x, pcmd.y, pw, ph, fbo.color_tex);
        }

        m_pip_queue.clear();
    }
    // Uses the same palette-indexed quad shader as rawblit, but with a per-video-frame
    // palette texture on unit 1 instead of the game palette.
    // The glClear() at the top of EndFrame already fills letterbox areas with black.
    if (m_fmv_pending)
    {
        const FmvBlitCmd& cmd = m_fmv_cmd;

        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Upload palette-index data. Use GL_UNPACK_ROW_LENGTH so FFmpeg frames
        // with linesize padding are handled correctly.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fmv_index_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, cmd.src_pitch);
        if (cmd.src_w != m_fmv_index_tex_w || cmd.src_h != m_fmv_index_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cmd.src_w, cmd.src_h,
                         0, GL_RED, GL_UNSIGNED_BYTE, cmd.px);
            m_fmv_index_tex_w = cmd.src_w;
            m_fmv_index_tex_h = cmd.src_h;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.src_w, cmd.src_h,
                            GL_RED, GL_UNSIGNED_BYTE, cmd.px);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // Upload per-video palette (BGRA data; GL swaps B/R into RGBA storage).
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_fmv_palette_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_BGRA, GL_UNSIGNED_BYTE, cmd.bgra_pal);

        // Build NDC rect for the pre-computed letterboxed/scaled destination.
        const float sw = (float)m_screenW;
        const float sh = (float)m_screenH;
        const float x0 = (float)cmd.dst_x               / sw * 2.0f - 1.0f;
        const float x1 = (float)(cmd.dst_x + cmd.dst_w) / sw * 2.0f - 1.0f;
        const float y0 = 1.0f - (float)cmd.dst_y               / sh * 2.0f;
        const float y1 = 1.0f - (float)(cmd.dst_y + cmd.dst_h) / sh * 2.0f;
        const float verts[6][4] = {
            { x0, y1,  0.f, 1.f },
            { x1, y1,  1.f, 1.f },
            { x1, y0,  1.f, 0.f },
            { x0, y1,  0.f, 1.f },
            { x1, y0,  1.f, 0.f },
            { x0, y0,  0.f, 0.f },
        };
        glBindVertexArray(m_fmv_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_fmv_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_fmv_pending = false;
    }

    // Landview zoom GPU blit — campaign-map zoom transition.
    // Draws map_screen as a fullscreen opaque quad; the fragment shader computes
    // each pixel's source texel from gl_FragCoord + per-frame zoom uniforms,
    // exactly matching frontzoom_to_point() arithmetic.
    // The transparent overlay (m_transparent_blit_pending) composites compressed_window_draw()
    // output on top with index-0-transparent blending after this pass.
    if (m_zoom_pending)
    {
        const LandviewZoomCmd& cmd = m_zoom_cmd;

        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Upload map_screen.  Use TexSubImage when dimensions are unchanged.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_zoom_tex);
        if (cmd.src_w != m_zoom_tex_w || cmd.src_h != m_zoom_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, cmd.src_w, cmd.src_h,
                         0, GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
            m_zoom_tex_w = cmd.src_w;
            m_zoom_tex_h = cmd.src_h;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.src_w, cmd.src_h,
                            GL_RED, GL_UNSIGNED_BYTE, cmd.src_buf);
        }

        // Game palette on unit 1.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        // Draw a fullscreen NDC quad; position/UV matches k_quadVerts convention.
        // The zoom fragment shader ignores v_uv and uses gl_FragCoord instead.
        static const float fs_quad[6][4] = {
            {-1.f, -1.f,  0.f, 1.f},
            { 1.f, -1.f,  1.f, 1.f},
            { 1.f,  1.f,  1.f, 0.f},
            {-1.f, -1.f,  0.f, 1.f},
            { 1.f,  1.f,  1.f, 0.f},
            {-1.f,  1.f,  0.f, 0.f},
        };
        // Reuse m_rawblit_vao — same VAO layout; zoom and rawblit are mutually
        // exclusive within a frame (only one land-view draw path fires per frame).
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fs_quad), fs_quad);

        glUseProgram(m_zoom_shader);
        glUniform2f(m_zoom_u_center_map, cmd.center_map_x, cmd.center_map_y);
        glUniform2f(m_zoom_u_screen_ctr, cmd.screen_cx,    cmd.screen_cy);
        glUniform1f(m_zoom_u_scale,      cmd.scale);
        glUniform2f(m_zoom_u_inv_map_sz, 1.0f / (float)cmd.src_w,
                                         1.0f / (float)cmd.src_h);
        glUniform1f(m_zoom_u_screen_h,   (float)m_screenH);

        glDisable(GL_BLEND);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_zoom_pending = false;
    }

    // Transparent overlay blit — composites SubmitTransparentBlit() data over the GPU
    // frame with index-0 transparency.  Used for the landview window frame.
    // Source data was already uploaded to m_texIndex inside SubmitTransparentBlit().
    if (m_transparent_blit_pending)
    {
        glViewport(0, 0, m_screenW, m_screenH);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texIndex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_shader);
        glUniform1f(m_uTintFactor, g_palette_possession_tint);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_transparent_blit_pending = false;
    }

    // Map-fade GPU compose pass — active during PVM_ParchFadeIn / ParchFadeOut.
    // GLMapFadePass::StepFadeIn/Out() records the current step without writing
    // to WScreen; this hook renders the native-resolution wipe quad on top of
    // the (empty) staging blit.  No-op for SoftwareMapFadePass.
    {
        IMapFadePass* mfp = RendererGetMapFadePass();
        if (mfp && mfp->HasGPUComposePass())
            mfp->RenderGPUComposePass();
    }

    // Flush layer-1 (front) GPU UI elements — escape menu, minimap, slab
    // selectors, power-hand — on top of everything else.
    UIRenderer_FlushFront();
    // GL error check after UI front pass
    {
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            static int s_cnt = 0;
            if (++s_cnt <= 20) SYNCLOG("FLICKER-DIAG: GL error 0x%X after FlushFront (%d)", err, s_cnt);
        }
    }

    // Text on top of all sprites (sidebar labels, event messages, tooltips).
    TextRenderer_Flush();

    // Screen-tint overlay — composites over all rendered layers (tiles, sprites,
    // UI, text). Driven by g_screen_tint set from palette-effect callbacks:
    // possession/pain (red), dungeon-heart death flash (white), zoom-to-heart.
    // No-op when alpha == 0 or no tint shader compiled.
    // Rendered BEFORE the cursor so the cursor is never tinted.
    if (g_screen_tint[3] > 0.0f && m_tintProg)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_tintProg);
        glUniform4fv(m_uTintColor, 1, g_screen_tint);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    // Cursor drawn last — after the screen-tint overlay — so it is always on
    // top of every other rendered layer including possession/death-flash tints.
    CursorLayer_Flush();

    RenderPass_EndFrame();
    platform_swap_gl_buffers(lbWindow);

    // Clear per-frame queues AFTER the swap — not in BeginFrame().
    // LbScreenSwap() can be called multiple times per game tick (fade loops,
    // loading screens, video playback).  If these queues are only cleared in
    // BeginFrame(), the second+ swap re-renders stale UI/cursor entries over
    // a glClear'd background (world geometry was already consumed on the first
    // swap), causing visible flicker.
    UIRenderer_Clear();
    CursorLayer_Clear();

    // Collect pending GPU timer query results for Tracy GPU zones.
    KFX_GPU_COLLECT();
    // Mark the end of this rendered frame in Tracy's timeline.
    KFX_FRAMEMARK();

    m_discard_cleared = false; // first LockFramebuffer next frame will zero the buffer
    m_frame_begun = false; // allow BeginFrame to run fully on the next frame
}

uint8_t* RendererOpenGL::LockFramebuffer(int* out_pitch)
{
    // Return the write-discard buffer so LbScreenLock() reports Lb_SUCCESS.
    // This keeps LbScreenLock-gated GPU paths (legal screen, menus, engine_redraw)
    // alive in GL mode. Content written here is never uploaded to the GPU.
    if (!m_discard_cleared && m_discardBuf)
    {
        memset(m_discardBuf, 0, (size_t)m_screenW * (size_t)m_screenH);
        m_discard_cleared = true;
    }
    if (out_pitch) *out_pitch = m_screenW;
    return m_discardBuf;
}

void RendererOpenGL::UnlockFramebuffer()
{
    // No-op in GL mode — discard buffer writes are silently ignored.
}

bool RendererOpenGL::BlitRaw8GPU(int dst_width, int dst_height, int dst_x, int dst_y,
                                  const unsigned char* src_buf, int src_width, int src_height)
{
    if (!m_rawblit_shader)
    {
        // Shader failed to compile during Init().  This is a fatal misconfiguration
        // in GL mode — no CPU fallback is permitted.
        ERRORLOG("RendererOpenGL::BlitRaw8GPU: shader not compiled; GPU blit dropped (src %dx%d)",
                 src_width, src_height);
        return false;
    }
    if (!src_buf || src_width <= 0 || src_height <= 0)
    {
        ERRORLOG("RendererOpenGL::BlitRaw8GPU: invalid source (buf=%p w=%d h=%d)",
                 (const void*)src_buf, src_width, src_height);
        return false;
    }
    // Queue — executed in EndFrame() after UIRenderer_FlushBack().
    m_rawblit_cmd            = { src_buf, src_width, src_height,
                                 dst_x, dst_y, dst_width, dst_height };
    m_rawblit_pending        = true;
    m_rawblit_cached         = true;
    m_rawblit_cached_cmd     = m_rawblit_cmd;
    return true;
}

bool RendererOpenGL::SubmitVideoFrame(
    const uint8_t* px, int src_w, int src_h, int src_pitch,
    const uint8_t* bgra_pal, int dst_x, int dst_y, int dst_w, int dst_h)
{
    if (!m_rawblit_shader)
    {
        ERRORLOG("RendererOpenGL::SubmitVideoFrame: shader not compiled; GPU path dropped");
        return false;
    }
    if (!px || !bgra_pal || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        ERRORLOG("RendererOpenGL::SubmitVideoFrame: invalid params (px=%p src=%dx%d dst=%dx%d)",
                 (const void*)px, src_w, src_h, dst_w, dst_h);
        return false;
    }
    m_fmv_cmd = { px, src_w, src_h, src_pitch, bgra_pal, dst_x, dst_y, dst_w, dst_h };
    m_fmv_pending = true;
    return true;
}

bool RendererOpenGL::SubmitLandviewZoom(
    const uint8_t* src_buf, int src_w, int src_h,
    float center_map_x, float center_map_y,
    float screen_cx,    float screen_cy,
    float scale)
{
    if (!m_zoom_shader)
    {
        ERRORLOG("RendererOpenGL::SubmitLandviewZoom: shader not compiled; GPU path dropped");
        return false;
    }
    if (!src_buf || src_w <= 0 || src_h <= 0)
    {
        ERRORLOG("RendererOpenGL::SubmitLandviewZoom: invalid params (src=%p %dx%d)",
                 (const void*)src_buf, src_w, src_h);
        return false;
    }
    m_zoom_cmd = { src_buf, src_w, src_h,
                   center_map_x, center_map_y,
                   screen_cx, screen_cy, scale };
    m_zoom_pending = true;
    return true;
}

bool RendererOpenGL::SubmitStagingOverlay()
{
    // Staging buffer has been removed; callers should use SubmitTransparentBlit().
    return false;
}

bool RendererOpenGL::SubmitTransparentBlit(const uint8_t* buf, int w, int h)
{
    if (w != m_screenW || h != m_screenH)
        return false;
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, buf);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_transparent_blit_pending = true;
    return true;
}

bool RendererOpenGL::SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                                        int dst_x, int dst_y, int dst_w, int dst_h)
{
    OverheadMapCmd cmd;
    cmd.pixels.assign(tile_colors, tile_colors + (size_t)tiles_x * (size_t)tiles_y);
    cmd.tiles_x = tiles_x;
    cmd.tiles_y = tiles_y;
    cmd.dst_x   = dst_x;
    cmd.dst_y   = dst_y;
    cmd.dst_w   = dst_w;
    cmd.dst_h   = dst_h;
    m_overhead_map_cmds.push_back(std::move(cmd));
    return true;
}

void RendererOpenGL::SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                                        int dst_x, int dst_y, int tile_w, int tile_h)
{
    if (!tile_block_ids || tiles_x <= 0 || tiles_y <= 0) return;

    // Record the bounding rect so EndFrame can fill it with solid black before
    // drawing tile quads.  This makes unrevealed and rock tiles (skipped with
    // 0xFFFF) appear as black rather than showing whatever is underneath.
    ZoomBoxBgCmd bg;
    bg.x = dst_x;
    bg.y = dst_y;
    bg.w = tiles_x * tile_w;
    bg.h = tiles_y * tile_h;
    m_zoom_box_bg_cmds.push_back(bg);

    m_zoom_tile_cmds.reserve(m_zoom_tile_cmds.size() + (size_t)tiles_x * tiles_y);
    for (int ty = 0; ty < tiles_y; ty++)
    {
        for (int tx = 0; tx < tiles_x; tx++)
        {
            uint16_t id = tile_block_ids[ty * tiles_x + tx];
            if (id == 0xFFFF) continue;  // unrevealed — skip
            float u0, v0, u1, v1;
            TileAtlasPacker::GetTileUV((int)id, &u0, &v0, &u1, &v1);
            ZoomTileCmd cmd;
            cmd.u0    = u0;  cmd.v0    = v0;
            cmd.u1    = u1;  cmd.v1    = v1;
            cmd.dst_x = dst_x + tx * tile_w;
            cmd.dst_y = dst_y + ty * tile_h;
            cmd.dst_w = tile_w;
            cmd.dst_h = tile_h;
            m_zoom_tile_cmds.push_back(cmd);
        }
    }
}

void RendererOpenGL::SubmitPiPRender(struct Camera* cam, int x, int y, int w, int h)
{
    if (!cam) return;
    PiPCmd cmd;
    cmd.cam_copy = *cam;
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    m_pip_queue.push_back(cmd);
}

void RendererOpenGL::ensure_pip_fbo(std::size_t idx, int w, int h)
{
    // Grow the slot vector if needed.
    if (idx >= m_pip_fbos.size())
        m_pip_fbos.resize(idx + 1);

    PiPFBO& slot = m_pip_fbos[idx];
    if (slot.fbo && slot.w == w && slot.h == h)
        return;  // Already correct size.

    // Destroy existing resources for this slot.
    if (slot.fbo)
    {
        glDeleteFramebuffers(1, &slot.fbo);
        glDeleteTextures(1, &slot.color_tex);
        glDeleteRenderbuffers(1, &slot.depth_rb);
        slot = PiPFBO{};
    }

    // Colour attachment.
    glGenTextures(1, &slot.color_tex);
    glBindTexture(GL_TEXTURE_2D, slot.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Depth renderbuffer.
    glGenRenderbuffers(1, &slot.depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, slot.depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Assemble FBO.
    glGenFramebuffers(1, &slot.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, slot.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.color_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, slot.depth_rb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        ERRORLOG("PiP FBO[%zu] incomplete (0x%x) — slot disabled", idx, (unsigned)status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &slot.fbo);
        glDeleteTextures(1, &slot.color_tex);
        glDeleteRenderbuffers(1, &slot.depth_rb);
        slot = PiPFBO{};
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    slot.w = w;
    slot.h = h;
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

    // Compile the screen-tint overlay program (flat-colour fullscreen quad).
    // Non-fatal: if this fails, palette effects (possession, white flash, etc.)
    // won't show on the 3D world pass but gameplay is unaffected.
    {
        std::string tv_src = get_embedded_shader_source("screen_tint_vert.glsl");
        std::string tf_src = get_embedded_shader_source("screen_tint_frag.glsl");
        if (!tv_src.empty() && !tf_src.empty())
        {
            unsigned int tv = compile_shader(GL_VERTEX_SHADER,   tv_src.c_str());
            unsigned int tf = compile_shader(GL_FRAGMENT_SHADER, tf_src.c_str());
            if (tv && tf)
            {
                m_tintProg = glCreateProgram();
                glAttachShader(m_tintProg, tv);
                glAttachShader(m_tintProg, tf);
                glLinkProgram(m_tintProg);
                glDeleteShader(tv);
                glDeleteShader(tf);
                m_uTintColor = glGetUniformLocation(m_tintProg, "u_tint_color");
            }
            else
            {
                if (tv) glDeleteShader(tv);
                if (tf) glDeleteShader(tf);
                WARNLOG("RendererOpenGL: screen-tint shader compile failed");
            }
        }
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
    glBindTexture(GL_TEXTURE_2D, m_texPalette);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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
