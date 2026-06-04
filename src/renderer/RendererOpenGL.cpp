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
#include "renderer/RendererThread.h"
#include "renderer/VecMath.h"
#include "renderer/opengl/GLTileAtlas.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include "renderer/opengl/GLWorldViewRenderer.h"
#include "renderer/opengl/GLUIRenderer.h"
#include "renderer/opengl/GLTextRenderer.h"
#include "renderer/opengl/GLMapFadePass.h"
#include "renderer/opengl/GLCursorLayer.h"
#include "renderer/opengl/GLShaders.h"
#include "renderer/opengl/GLLensPass.h"
#include "kfx/profiling/KfxProfiling.h"
#include "kfx/lense/LensManager.h"
#include "kfx/lense/LensEffect.h"
#include "renderer/IPostProcessPass.h"
#include "renderer/RendererHelper.h"

#include "bflib_video.h"    // lbDisplay, RendererGetScreenWidth()/Height
#include "bflib_sprite.h"   // TbSpriteSheet, get_sprite
#include "platform.h"       // platform_create_gl_context / swap / destroy
#include "engine_textures.h" // update_animating_texture_maps()
#include "engine_render.h"   // draw_view()
#include "engine_redraw.h"   // setup_engine_window / store_engine_window (PiP viewport)
#include "engine_lenses.h"   // lens_mode
#include "vidfade.h"         // g_palette_possession_tint

#include <glad/glad.h>
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

void APIENTRY DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                            const GLchar* message, const void* userParam) {
    // Ignore noisy non‑issues (optional)
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204)
        return;

    const char* src = source == GL_DEBUG_SOURCE_API               ? "API"
                      : source == GL_DEBUG_SOURCE_WINDOW_SYSTEM   ? "WindowSys"
                      : source == GL_DEBUG_SOURCE_SHADER_COMPILER ? "ShaderCompiler"
                      : source == GL_DEBUG_SOURCE_THIRD_PARTY     ? "3rdParty"
                      : source == GL_DEBUG_SOURCE_APPLICATION     ? "Application"
                      : source == GL_DEBUG_SOURCE_OTHER           ? "Other"
                                                                  : "Unknown";

    const char* tp = type == GL_DEBUG_TYPE_ERROR                 ? "Error"
                     : type == GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR ? "Deprecated"
                     : type == GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  ? "Undefined"
                     : type == GL_DEBUG_TYPE_PORTABILITY         ? "Portability"
                     : type == GL_DEBUG_TYPE_PERFORMANCE         ? "Performance"
                     : type == GL_DEBUG_TYPE_MARKER              ? "Marker"
                     : type == GL_DEBUG_TYPE_PUSH_GROUP          ? "PushGroup"
                     : type == GL_DEBUG_TYPE_POP_GROUP           ? "PopGroup"
                     : type == GL_DEBUG_TYPE_OTHER               ? "Other"
                                                                 : "Unknown";

    const char* sev = severity == GL_DEBUG_SEVERITY_HIGH           ? "HIGH"
                      : severity == GL_DEBUG_SEVERITY_MEDIUM       ? "MEDIUM"
                      : severity == GL_DEBUG_SEVERITY_LOW          ? "LOW"
                      : severity == GL_DEBUG_SEVERITY_NOTIFICATION ? "NOTIFY"
                                                                   : "Unknown";
    JUSTLOG("[GL DEBUG] Severity=%s Type=%s Source=%s Id=%u Message=%s", sev, tp, src, id, message);
}

bool RendererOpenGL::Init()
{
    RendererThread_RegisterGameThread();

    // Create GL context (SDL2-based on desktop; see platform_gl_sdl2.cpp)
    if (!platform_create_gl_context(platform_get_sdl_window()))
    {
        ERRORLOG("RendererOpenGL::Init: failed to create GL context");
        return false;
    }

    // Load GL function pointers via glad
    if (!gladLoadGLLoader((GLADloadproc)platform_gl_get_proc_address))
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
    m_screenW = RendererGetScreenWidth();
    m_screenH = RendererGetScreenHeight();

    // Transparent overlay texture — GL_R8, screen-sized.
    // SubmitTransparentBlit() uploads directly into this texture; EndFrame composites
    // it over the GPU frame with index-0 transparency (for landview window frame etc.).
    glGenTextures(1, &m_texIndex);
    KFX_GL_LABEL(GL_TEXTURE, m_texIndex, "Blit/TransparentOverlayTex");
    glBindTexture(GL_TEXTURE_2D, m_texIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_screenW, m_screenH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    m_texIndex_w = m_screenW;
    m_texIndex_h = m_screenH;

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
    // Fade table texture is initialised later via RendererNotifyGameTablesReady()
    // (called after setup_stuff()) because render_fade_tables is not yet loaded.

    if (!init_tile_atlas())
    {
        SYNCDBG(7, "RendererOpenGL: tile atlas deferred — block_mem not yet loaded");
    }

    // ── Raw-image GPU blit (frontend background images) ────────────────────────
    // Compile shader: reuse palette_blit_vert.glsl + rawimage_blit_frag.glsl.
    // Fatal if shader compilation fails — no CPU fallback is permitted in GL mode.
    {
        unsigned int rv = compile_shader(GL_VERTEX_SHADER,   PALETTE_BLIT_VERTEX_SHADER);
        unsigned int rf = compile_shader(GL_FRAGMENT_SHADER, RAWIMAGE_BLIT_FRAGMENT_SHADER);
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
        std::string rv_src = PALETTE_BLIT_VERTEX_SHADER;
        std::string rf_src = OVERHEAD_MAP_FRAGMENT_SHADER;
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
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_index"),     0);
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_palette"),   1);
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_fade"),      2);
                    glUniform1i(glGetUniformLocation(m_overhead_map_shader, "u_parchment"), 3);
                    m_omap_loc_map_rect    = glGetUniformLocation(m_overhead_map_shader, "u_map_rect");
                    m_omap_loc_screen_size = glGetUniformLocation(m_overhead_map_shader, "u_screen_size");
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
        std::string rv_src = PALETTE_BLIT_VERTEX_SHADER;
        std::string rf_src = ZOOM_TILE_FRAGMENT_SHADER;
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
                    m_uZoomClipRect   = glGetUniformLocation(m_zoom_tile_shader, "u_clip_rect");
                    m_uZoomClipRadius = glGetUniformLocation(m_zoom_tile_shader, "u_clip_radius");
                    m_uZoomClipScrH   = glGetUniformLocation(m_zoom_tile_shader, "u_clip_screen_h");
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
        unsigned int zv = compile_shader(GL_VERTEX_SHADER,   PALETTE_BLIT_VERTEX_SHADER);
        unsigned int zf = compile_shader(GL_FRAGMENT_SHADER, LANDVIEW_ZOOM_FRAGMENT_SHADER);
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

    // 1×1 black R8 fallback — bound to sampler units that lack a real texture,
    // ensuring all samplers reference a complete texture object at all times.
    {
        glGenTextures(1, &m_tex_null);
        glBindTexture(GL_TEXTURE_2D, m_tex_null);
        const uint8_t null_px = 0;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &null_px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
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
    else
    {
        SYNCLOG("RendererOpenGL: UI sprite atlas initialized successfully");
    }

    // Pre-allocate IR command buffer backing memory for both frame sides so
    // neither triggers a heap realloc during a live frame.
    // Estimates: 4096 world tiles, 2048 world sprites, 512 shadows,
    //            512 UI commands, 256 text glyphs.
    m_render_graph.Reserve(4096, 2048, 512,  /* world_tiles, sprites, shadows */
                           512,  256,        /* ui_cmds, text_cmds */
                           0,    0);         /* shadow_cmds, debug_cmds */

    // ToDo : disable this before i lose my fucking nut.
#ifdef DEBUG
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

    // Check if debug callback is supported before using it
    if (glDebugMessageCallback != nullptr)
    {
        glDebugMessageCallback(DebugCallback, nullptr);
        // Optional: enable everything
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }
    else
    {
        WARNLOG("RendererOpenGL::Init: GL_KHR_debug not available; debug callback disabled");
    }
#endif // DEBUG

    return true;
}

void RendererOpenGL::Shutdown()
{
    // Signal the render thread to quit and join it before any GL cleanup.
    // The render thread releases the context on exit (cleanup_fn), allowing the
    // main thread to re-acquire it for glDelete* calls below.
    if (m_render_thread.IsActive())
    {
        m_render_thread.Stop();
        platform_gl_acquire_context();
    }

    // Clear cached rawblit data to prevent dangling pointers across screen mode changes.
    // The cached command may contain a pointer to a frontend image buffer that becomes
    // invalid when the renderer is shut down/reinitialized during screen mode transitions.
    m_rawblit_cached = false;
    m_rawblit_pending = false;
    m_rawblit_cached_cmd = {};
    m_rawblit_cmd = {};

    delete m_tile_atlas;
    m_tile_atlas = nullptr;

    delete m_sprite_atlas;
    m_sprite_atlas = nullptr;

    if (m_vao)     { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo);        m_vbo = 0; }
    if (m_shader)  { glDeleteProgram(m_shader);          m_shader = 0; }
    if (m_tintProg){ glDeleteProgram(m_tintProg);        m_tintProg = 0; }
    if (m_texIndex)   { glDeleteTextures(1, &m_texIndex);   m_texIndex = 0; }
    if (m_texPalette) { glDeleteTextures(1, &m_texPalette); m_texPalette = 0; }
    if (m_texFade)    { glDeleteTextures(1, &m_texFade);    m_texFade = 0; }
    if (m_tex_null)   { glDeleteTextures(1, &m_tex_null);   m_tex_null = 0; }
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

    // Lens post-process FBOs
    if (m_lens_scene_fbo)      { glDeleteFramebuffers(1, &m_lens_scene_fbo);       m_lens_scene_fbo = 0; }
    if (m_lens_scene_tex)      { glDeleteTextures(1, &m_lens_scene_tex);           m_lens_scene_tex = 0; }
    if (m_lens_scene_depth_rb) { glDeleteRenderbuffers(1, &m_lens_scene_depth_rb); m_lens_scene_depth_rb = 0; }
    if (m_lens_pass_fbo_a)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_a);      m_lens_pass_fbo_a = 0; }
    if (m_lens_pass_tex_a)     { glDeleteTextures(1, &m_lens_pass_tex_a);          m_lens_pass_tex_a = 0; }
    if (m_lens_pass_fbo_b)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_b);      m_lens_pass_fbo_b = 0; }
    if (m_lens_pass_tex_b)     { glDeleteTextures(1, &m_lens_pass_tex_b);          m_lens_pass_tex_b = 0; }
    if (m_passthrough_shader)  { glDeleteProgram(m_passthrough_shader);             m_passthrough_shader = 0; }
    m_lens_fbo_w = 0;
    m_lens_fbo_h = 0;

    // Swipe overlay shader + VAO/VBO
    if (m_swipe_shader) { glDeleteProgram(m_swipe_shader); m_swipe_shader = 0; }
    if (m_swipe_vao)    { glDeleteVertexArrays(1, &m_swipe_vao); m_swipe_vao = 0; }
    if (m_swipe_vbo)    { glDeleteBuffers(1, &m_swipe_vbo); m_swipe_vbo = 0; }

    platform_destroy_gl_context();
}

// (RenderThreadProc has been replaced by lambdas passed to RenderThreadManager::Start()
//  in EndFrame().  See RendererOpenGL::EndFrame() below.)

void RendererOpenGL::ClearScreen(uint8_t colour_index)
{
    m_clearColourIndex = colour_index;
}

bool RendererOpenGL::BeginFrame()
{
    // Idempotent: multiple RendererLockScreen calls per frame must not clear the UI queue again.
    if (m_frame_begun) return true;

    // If a deferred sprite-atlas rebuild is pending, drain it now — before any
    // sprite handles are issued for this frame — so the generation bump is invisible
    // to the game code that follows.  WaitForCompletion() ensures the render thread
    // has finished consuming the previous frame's IR (old-generation handles) before
    // Rebuild() increments the generation.  This is the only safe window: after the
    // render thread retires the old atlas, and before new handles are stamped.
    if (RendererHasDeferredAtlasRebuild())
    {
        m_render_thread.WaitForCompletion();
        RendererDrainDeferredAtlasRebuild();
    }

    m_frame_begun = true;

    // Reset IR write-side buffers for this frame so sub-renderers start clean.
    m_render_graph.BeginFrame();

    // Refresh cached screen dimensions — screen mode may have changed since Init().
    // These are game-thread-only values; the render thread reads from m_rt_frame_state.
    if (RendererGetScreenWidth() > 0 && RendererGetScreenHeight() > 0)
    {
        m_screenW = (int)RendererGetScreenWidth();
        m_screenH = (int)RendererGetScreenHeight();
    }

    // NOTE: sub-renderer SetScreenSize() is NOT called here (game thread).
    // Pushing dimensions from BeginFrame races the render thread reading them in
    // EndFrame_GL().  Screen size is snapshotted into m_rt_frame_state.screen_w/h
    // in FlipBuffers() and pushed to sub-renderers at the start of EndFrame_GL()
    // where the render thread has exclusive access.

    // Lazy-retry resources that depend on level data loaded after Init().
    // After the first EndFrame() the GL context lives on the render thread, so
    // we cannot call init_tile_atlas() here (it issues glGenTextures / glTexImage3D).
    // Set the pending flag and let EndFrame_GL() handle the GPU work on the
    // thread that owns the context.
    if (m_tile_atlas && !m_tile_atlas->IsInitialized())
        m_tile_atlas_init_pending = true;

    if (m_tile_atlas && m_tile_atlas->IsInitialized() && m_world_renderer)
        m_world_renderer->TryEarlyInit();

    // Re-upload the animated tile atlas rows only when the game-logic tick has
    // actually advanced the animation (update_animating_texture_maps() called from
    // main.cpp swaps block_ptrs pointers, changing the sentinel value).
    // This avoids expensive palette→RGBA8 decodes + GPU uploads at the render
    // frame rate (60 fps); uploads now happen at the game-tick rate only.
    // Defer the GL upload to EndFrame_GL() — the render thread owns the context.
    if (m_tile_atlas && m_tile_atlas->IsInitialized())
    {
        const uint8_t* anim_sentinel = block_ptrs[TEXTURE_BLOCKS_STAT_COUNT_A];
        if (anim_sentinel != m_last_anim_sentinel)
        {
            m_anim_tiles_dirty = true;
            m_last_anim_sentinel = anim_sentinel;
        }
    }

    // NOTE: RenderPass_BeginFrame() is NOT called here — it resets OpenGLSpriteBackend
    // state (m_quads, m_remap_cache, etc.) that is exclusively written and read by the
    // render thread during draw_3d_sprites_for_bucket() / DrawNow().  Calling it here
    // (game thread) would race with the render thread still processing the previous
    // frame's sprites.  It is called at the top of EndFrame_GL() instead.
    //
    // Don't clear UI/cursor queues during palette fades — RendererPresentFrame()
    // is called in a tight loop with no draw_gui() in between.  The queues keep
    // their content so the sidebar stays visible while the palette darkens.
    if (!RendererIsFadeCachePreserved())
    {
        UIRenderer_Clear();
        CursorLayer_Clear();
        // Open the IR write window: all Submit*() calls this frame will append
        // IR commands.  Closed in EndFrame() before FlipBuffers().
        auto* ui = RendererGetUIRenderer();
        if (ui) ui->SetUICommandBuffers(&m_render_graph.GetUIBuffers());

        // Open the cursor IR write window.
        auto* cursor = RendererGetCursorLayer();
        if (cursor) cursor->SetCursorWriteBuffers(&m_render_graph.GetUIBuffers());

        // Open the world IR write window (sentinel; internal state still drives execution).
        if (m_world_renderer) m_world_renderer->SetWorldCommandBuffers(&m_render_graph.GetWorldBuffers());
    }

    // Text write window is always opened — text commands are freshly submitted
    // every frame.  During fade-preserve frames the submitted text goes to the
    // write buffer (safe), and UpdateFrameState() leaves the read-side text IR
    // unchanged so the render thread re-displays the preserved sidebar text.
    // Without this, any LbTextDrawResized() call during a fade-preserve frame
    // would append to m_pending on the game thread while the render thread
    // concurrently reads m_pending in ExecuteTextFromIR() — a data race.
    if (m_textRenderer) m_textRenderer->SetTextCommandBuffers(&m_render_graph.GetTextBuffers());
    return true;
}

void RendererOpenGL::EndFrame()
{
    // Phase 3C: wait for the PREVIOUS frame's render to complete before we
    // flip the command buffers for the new frame.  m_work_done starts true
    // so the very first call passes through immediately (no prior frame).
    m_render_thread.WaitForCompletion();

    // Drain any deferred sprite-atlas rebuild not already handled by BeginFrame().
    // BeginFrame() now drains when s_rebuild_deferred is set (the common load-time
    // case), so this call is a no-op for the normal path.  It remains here as a
    // fallback for the rare case where RendererNotifySpritesReloaded() is called
    // mid-frame (after BeginFrame() but before EndFrame()); in that scenario the
    // rebuild fires here and causes a one-frame sprite drop — acceptable for an
    // in-flight reload that does not happen during normal gameplay.
    RendererDrainDeferredAtlasRebuild();

    // Lazily start the render thread on the first EndFrame() call.
    // All sub-renderer GL initialisation (GLWorldViewRenderer, GLTextRenderer,
    // GLMapFadePass, etc.) runs on the main thread with the context current
    // between Init() and this point.  On the first EndFrame() we release the
    // context from the main thread and hand it to the render thread permanently.
    if (!m_render_thread.IsActive())
    {
        platform_gl_release_context();
        m_render_thread.Start(
            // init_fn: acquire GL context on the render thread, init Tracy.
            [this]() {
                platform_gl_acquire_context();
                if (!platform_is_renderdoc_present()) {
                    KFX_GPU_CTX_CREATE();
                } else {
                    SYNCLOG("RenderDoc detected — Tracy GPU profiling disabled (render thread)");
                }
            },
            // work_fn: execute one frame of GL submission.
            [this]() { EndFrame_GL(); },
            // cleanup_fn: release context so Shutdown() can re-acquire it.
            [this]() { platform_gl_release_context(); }
        );
    }

    // Close the IR write window before flipping buffers so no Submit*()
    // calls from the next frame land in this frame's command buffer.
    auto* ui_close = RendererGetUIRenderer();
    if (ui_close) ui_close->SetUICommandBuffers(nullptr);
    if (auto* cursor_close = RendererGetCursorLayer()) cursor_close->SetCursorWriteBuffers(nullptr);
    if (m_world_renderer) m_world_renderer->SetWorldCommandBuffers(nullptr);
    if (m_textRenderer) m_textRenderer->SetTextCommandBuffers(nullptr);

    // Snapshot per-frame command queues and scalar state into render-thread
    // copies before signalling.  The game thread returns from EndFrame()
    // immediately and may call SubmitPiPRender(), BlitRaw8GPU(), etc. for the
    // next frame while EndFrame_GL() is still running — these rt_ copies
    // eliminate the resulting data races on std::vector and struct fields.
    m_rt_pip_queue           = std::move(m_pip_queue);
    m_rt_overhead_map_cmds   = std::move(m_overhead_map_cmds);
    m_rt_rawblit_pending     = m_rawblit_pending;
    m_rt_rawblit_cmd         = m_rawblit_cmd;
    m_rt_rawblit_cached      = m_rawblit_cached.load(std::memory_order_relaxed);
    m_rt_rawblit_cached_cmd  = m_rawblit_cached_cmd;
    m_rawblit_pending        = false;
    m_rt_clearColourIndex    = m_clearColourIndex;
    m_clearColourIndex       = 0;

    // Snapshot the four command categories that were previously missing from
    // FlipBuffers() and therefore raced between the game thread and EndFrame_GL().
    // FMV: pixel/pal buffers are already owned (copied in SubmitVideoFrame); move.
    m_rt_fmv_pending  = m_fmv_pending;
    m_rt_fmv_px_buf   = std::move(m_fmv_px_buf);
    m_rt_fmv_pal_buf  = std::move(m_fmv_pal_buf);
    m_rt_fmv_cmd      = m_fmv_cmd;
    m_rt_fmv_cmd.px       = m_rt_fmv_px_buf.empty()  ? nullptr : m_rt_fmv_px_buf.data();
    m_rt_fmv_cmd.bgra_pal = m_rt_fmv_pal_buf.empty() ? nullptr : m_rt_fmv_pal_buf.data();
    m_fmv_pending     = false;
    m_fmv_cmd         = {};
    // Landview zoom: source buffer is owned (copied in SubmitLandviewZoom); move.
    m_rt_zoom_pending  = m_zoom_pending;
    m_rt_zoom_src_buf  = std::move(m_zoom_src_buf);
    m_rt_zoom_cmd      = m_zoom_cmd;
    m_rt_zoom_cmd.src_buf = m_rt_zoom_src_buf.empty() ? nullptr : m_rt_zoom_src_buf.data();
    m_zoom_pending     = false;
    m_zoom_cmd         = {};
    // Transparent blit: buffer is already owned (copied in SubmitTransparentBlit).
    m_rt_transparent_blit_pending = m_transparent_blit_pending;
    m_rt_transparent_blit_buf     = std::move(m_transparent_blit_buf);
    m_transparent_blit_pending    = false;
    // Zoom-box tile quads + background fill rects.
    m_rt_zoom_tile_cmds   = std::move(m_zoom_tile_cmds);
    m_rt_zoom_box_bg_cmds = std::move(m_zoom_box_bg_cmds);
    std::copy(std::begin(m_zoom_clip_rect), std::end(m_zoom_clip_rect),
              std::begin(m_rt_zoom_clip_rect));
    m_rt_zoom_clip_radius = m_zoom_clip_radius;

    // Double-buffer swipe overlay verts: same pattern as PiP/rawblit queues.
    m_rt_swipe_verts = std::move(m_swipe_verts);

    // Screenshot: move pending path/fmt into render-thread copies; clear pending
    // so that subsequent EndFrame() calls without a screenshot don't re-trigger.
    m_rt_screenshot_path = std::move(m_pending_screenshot_path);
    m_rt_screenshot_fmt  = m_pending_screenshot_fmt;
    m_pending_screenshot_path.clear();
    m_pending_screenshot_fmt = 0;

    // Snapshot all game-thread globals that EndFrame_GL() needs.
    // Must happen here (before signalling) so the render thread never reads live
    // globals that the game thread can modify concurrently once we return.
    std::memcpy(m_rt_frame_state.palette, lbPalette, sizeof(m_rt_frame_state.palette));
    m_rt_frame_state.possession_tint = g_palette_possession_tint;
    std::memcpy(m_rt_frame_state.screen_tint, g_screen_tint, sizeof(m_rt_frame_state.screen_tint));
    m_rt_frame_state.lens_mode = lens_mode;
    m_rt_frame_state.screen_w  = m_screenW;
    m_rt_frame_state.screen_h  = m_screenH;

    // Flush the map-fade IR command to the render graph's write-side
    // post-process buffers. FlushToRenderGraph() checks view_mode (game thread
    // safe), emits an IRMapFadeCmd if a transition is active, and clears
    // m_capture_pending. Must happen before Flip()/UpdateFrameState().
    if (auto* mfp = RendererGetMapFadePass()) mfp->FlushToRenderGraph(m_render_graph);

    {
        const UICommandBuffers& wui [[maybe_unused]] = m_render_graph.GetWriteUIBuffers();
        SYNCDBG(1, "EndFrame: fade=%d ir_active=%d solid=%zu slabbg=%zu sprites=%zu sprR=%zu sprC=%zu "
                   "slabsel=%zu cptr=%zu chand=%zu",
                (int)RendererIsFadeCachePreserved(),
                (int)wui.ir_active,
                wui.solid_boxes.Size(),   wui.slab_backgrounds.Size(),
                wui.sprites.Size(),       wui.sprites_remap.Size(),
                wui.sprites_colored.Size(), wui.slab_selectors.Size(),
                wui.cursor_pointers.Size(), wui.cursor_hands.Size());
    }

    if (RendererIsFadeCachePreserved())
    {
        // Palette-fade loop: preserve the render thread's read-side IR unchanged
        // so it re-draws the same UI/world geometry as the last real frame.
        // Only update FrameState so palette darkening / tint changes are visible.
        SYNCDBG(1, "EndFrame: UpdateFrameState (no flip) — preserving previous UI");
        m_render_graph.UpdateFrameState(m_rt_frame_state);
    }
    else
    {
        // IMPORTANT: always flip on non-fade frames, even when write-side
        // command buffers are empty. Preserving stale read-side IR across
        // transitions can replay old text commands that carry dangling font
        // pointers after resource reload/teardown (seen during quit).
        //
        // Normal frame: flip sub-renderer command buffers so the render thread
        // reads from stable render-side copies while the game thread builds N+1.
        if (m_world_renderer)
            m_world_renderer->FlipBuffers();

        if (ui_close) ui_close->FlipBuffers();

        // Flip IR command buffers: atomically swap write↔read and latch the
        // FrameState snapshot so the render thread reads from stable copies.
        m_render_graph.Flip(m_rt_frame_state);
    }

    // Signal the render thread to execute EndFrame_GL() (GL submission + swap).
    // Phase 3C: asynchronous — game thread returns immediately after signalling.
    // The wait for completion has moved to the TOP of this function.
    m_render_thread.Signal();

    // Reset the frame-begun flag HERE (game thread).
    m_frame_begun = false;
}

void RendererOpenGL::FlushRenderWork()
{
    m_render_thread.WaitForCompletion();
}

void RendererOpenGL::EndFrame_GL()
{
    // Deferred tile-atlas GPU init — runs on the render thread that owns the
    // GL context.  BeginFrame() (game thread) cannot call glGenTextures /
    // glTexImage3D directly because the context has already been transferred
    // here after the first EndFrame().
    if (m_tile_atlas_init_pending && m_tile_atlas && !m_tile_atlas->IsInitialized())
    {
        if (init_tile_atlas())
            m_tile_atlas_init_pending = false;
        // else: block_mem still not ready — keep pending, retry next frame.
    }
    else if (m_tile_atlas_init_pending)
    {
        m_tile_atlas_init_pending = false; // already initialized or atlas missing
    }

    // Deferred fade-table GPU init.
    // RendererNotifyGameTablesReady() is always called after the first EndFrame()
    // (loading-screen frames fire before setup_stuff()), so the GL context is
    // already on the render thread when it runs.  The actual texture creation
    // must happen here where the context is current.
    if (m_fade_table_pending)
    {
        m_fade_table_pending = false;
        if (init_fade_table_texture())
        {
            if (m_world_renderer) m_world_renderer->SetFadeTexture(m_texFade);
            auto* ui = RendererGetUIRenderer();
            if (ui) ui->SetFadeTexture(static_cast<GpuTextureHandle>(m_texFade));
            SYNCLOG("EndFrame_GL: fade-table texture created (tex=%u)", m_texFade);
        }
    }

    // Flush any pending GL work in sub-renderers (slab texture, etc.).
    {
        IUIRenderer* ui = RendererGetUIRenderer();
        if (ui) ui->FlushPendingInit();
    }

    // Flush deferred sprite-atlas GL work (glGenTextures/glTexImage2D/glDeleteTextures
    // deferred from RendererNotifySpritesReloaded, which runs on the game thread).
    RendererFlushPendingSpriteAtlas();
    SYNCDBG(0, "EndFrame_GL step 1: sprite atlas flush done");

    // Upload animated tile strips if BeginFrame() detected a game-tick animation
    // advance.  The GL upload was deferred to this thread because the render thread
    // owns the GL context; BeginFrame() runs on the game thread.
    if (m_anim_tiles_dirty && m_tile_atlas && m_tile_atlas->IsInitialized())
    {
        m_tile_atlas->UpdateAnimatedTiles();
        m_anim_tiles_dirty = false;
    }

    // Upload palette unconditionally — it may have changed this frame via LbPaletteSet.
    // Palette switches happen rarely (level load, possession), so the overhead of a
    // 1 KB CPU expand + glTexSubImage2D is negligible compared to other frame work.
    upload_palette_texture();
    SYNCDBG(0, "EndFrame_GL step 2: palette upload done");

    // Push snapshotted screen dimensions to all sub-renderers (render thread only —
    // eliminates the BeginFrame race where the game thread wrote these while the
    // render thread was still reading them for the previous frame's draw calls).
    {
        const int sw = m_rt_frame_state.screen_w;
        const int sh = m_rt_frame_state.screen_h;
        if (m_world_renderer)   m_world_renderer->SetScreenSize(sw, sh);
        if (m_gl_ui_renderer)   m_gl_ui_renderer->SetScreenDimensions(sw, sh);
        if (m_textRenderer)     m_textRenderer->SetScreenSize(sw, sh);
        if (m_gl_mapfade)       m_gl_mapfade->SetScreenSize(sw, sh);
    }

    glDisable(GL_SCISSOR_TEST);
    {
        // Resolve palette index → RGBA for the GL clear colour (6-bit palette: shift left 2 for 8-bit).
        const float r = (float)(m_rt_frame_state.palette[m_rt_clearColourIndex * 3 + 0] << 2) / 255.0f;
        const float g = (float)(m_rt_frame_state.palette[m_rt_clearColourIndex * 3 + 1] << 2) / 255.0f;
        const float b = (float)(m_rt_frame_state.palette[m_rt_clearColourIndex * 3 + 2] << 2) / 255.0f;
        glClearColor(r, g, b, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    SYNCDBG(0, "EndFrame_GL step 3: glClear done");

    // ── RenderGraph dispatch ──────────────────────────────────────────────────
    // Execute() performs the first real dispatch step:
    //   • UI: calls ui->PopulateFromIR() to fill m_rt_quads[]/m_rt_lines[] from
    //     the IR snapshot.  No GL draws here; DrawBack/DrawFront below issue them.
    //   • World + Text: dispatched explicitly below (ordering constraints require
    //     world inside the lens-FBO bracket; text after DrawFrontOverlay).
    m_render_graph.Execute(GetCapabilities(),
                           m_world_renderer,
                           RendererGetUIRenderer(),
                           m_textRenderer,
                           nullptr,  // IShadowRenderer — not yet implemented
                           nullptr); // IDebugRenderer  — not yet implemented

    // ── Lens FBO redirect
    // redirect world rendering to the lens scene FBO so GPU post-process
    // passes can operate on the result before it reaches the screen.
    m_lens_active = false;
    {
        LensManager* lm = LensManager::GetInstance();
        if (m_rt_frame_state.lens_mode == 2 && lm && lm->IsReady())
        {
            // Check if any effect actually provides a GPU pass
            bool has_gpu_pass = false;
            for (LensEffect* e : lm->GetEffects())
            {
                if (e->IsEnabled() && e->GetGPUPass())
                {
                    has_gpu_pass = true;
                    break;
                }
            }
            if (has_gpu_pass)
            {
                EnsureLensFBOs();
                if (m_lens_scene_fbo)
                {
                    glBindFramebuffer(GL_FRAMEBUFFER, m_lens_scene_fbo);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    m_lens_active = true;
                }
            }
        }
    }

    // Render GPU world geometry + depth-correct sprites.
    // Runs BEFORE the staging buffer upload so both layers composite correctly.
    // When m_lens_active, output goes to m_lens_scene_fbo instead of default FB.
    if (m_world_renderer)
    {
        m_world_renderer->ExecuteWorldFromIR(m_render_graph.GetWorldBuffersRT());
    }

    // Draw swipe-overlay quads while the lens FBO is still bound,
    // so they sit on top of world geometry and get lens-distorted.
    FlushSwipeQuads();
    SYNCDBG(0, "EndFrame_GL step 4: swipe quads + world render done");

    // If lens FBO was active, apply GPU post-process passes and blit result
    // back to the default framebuffer.
    if (m_lens_active)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ApplyLensGPUPasses();
        m_lens_active = false;
    }

    // Map-fade GPU compose pass — active during PVM_ParchFadeIn / ParchFadeOut.
    // Must run HERE — after ExecuteWorldFromIR() (so the dungeon view is already
    // on the default framebuffer for glBlitFramebuffer capture) but BEFORE the
    // rawblit/overhead map draws (so those queues are still available for the
    // parchment FBO capture inside CaptureAndUploadFrames).
    // IRMapFadeCmd is written by GLMapFadePass::FlushToRenderGraph() on the
    // game thread and consumed from the render-side post-process buffers here.
    {
        const auto& post_process = m_render_graph.GetPostProcessBuffersRT();
        if (post_process.map_fade)
        {
            IMapFadePass* mfp = RendererGetMapFadePass();
            if (mfp) mfp->ExecuteFromIR(*post_process.map_fade);
        }
    }

    // m_rt_quads[]/m_rt_lines[] have been populated by Execute() above via
    // ui->PopulateFromIR().  DrawBack()/DrawFront() below issue the actual draws.

    // Draw layer-0 (back) GPU UI elements — sidebar background panels.
    UIRenderer_DrawBack();

    // Raw-image GPU blit — frontend background images (legal, loading, menu bg,
    // map bg, torture, etc.).  Queued by BlitRaw8GPU() during the frame; drawn
    // here as an opaque quad so that the staging-blit overlay (which composites
    // any CPU-drawn menu sprites above index 0) lands on top.
    // During blocking palette-fade loops (ProperFadePalette / fade_in / fade_out),
    // the game calls RendererPresentFrame repeatedly without issuing any new draw commands.
    // Re-issue the last frontend rawblit with the freshly-uploaded (darkened) palette
    // so the fade is visible in GPU mode.  Only kicks in when nothing new was queued
    // AND the fade-cache preserve flag is active (set by ProperFadePalette).
    if (!m_rt_rawblit_pending && m_rt_rawblit_cached && RendererIsFadeCachePreserved())
        m_rt_rawblit_pending = true, m_rt_rawblit_cmd = m_rt_rawblit_cached_cmd;

    if (m_rt_rawblit_pending)
    {
        const RawBlitCmd& cmd = m_rt_rawblit_cmd;

        // Ensure full-screen viewport and no depth interaction.
        // UIRenderer_DrawBack() returns early (no quads) on pure-frontend frames
        // without disabling depth test, so we must guard here explicitly.
        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
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

        // m_texPalette is always bound to unit 1; rebind explicitly (upload_palette_texture
        // restores active unit to GL_TEXTURE0 on exit, but the binding on unit 1 persists).
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        // Build a rect quad in NDC covering [dst_x..dst_x+dst_w] x [dst_y..dst_y+dst_h].
        // Screen-space: y increases downward; NDC: y increases upward.
        const float sw = (float)m_rt_frame_state.screen_w;
        const float sh = (float)m_rt_frame_state.screen_h;
        Vec2f ndc0 = ScreenToNDC((float)cmd.dst_x,              (float)cmd.dst_y,              sw, sh);
        Vec2f ndc1 = ScreenToNDC((float)(cmd.dst_x + cmd.dst_w), (float)(cmd.dst_y + cmd.dst_h), sw, sh);

        // Two triangles; UV (0,0) = top-left, (1,1) = bottom-right.
        const float verts[6][4] = {
            { ndc0.x, ndc1.y,  0.f, 1.f },  // bottom-left
            { ndc1.x, ndc1.y,  1.f, 1.f },  // bottom-right
            { ndc1.x, ndc0.y,  1.f, 0.f },  // top-right
            { ndc0.x, ndc1.y,  0.f, 1.f },  // bottom-left
            { ndc1.x, ndc0.y,  1.f, 0.f },  // top-right
            { ndc0.x, ndc0.y,  0.f, 0.f },  // top-left
        };
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Restore depth mask and reset active texture unit to 0 so the
        // subsequent staging blit and other passes start from a clean state.
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);

        m_rt_rawblit_pending = false;
    }
    else if (!RendererIsFadeCachePreserved())
    {
        // No rawblit submitted this frame and no palette-fade in progress.
        // Clear the stale cache so old menu backgrounds don't persist after
        // transitioning from frontend to in-game.
        m_rawblit_cached = false;
    }

    // Overhead map tile colour GPU blit — drawn after the parchment background
    // rawblit and before the staging overlay so tile colours sit below CPU sprites
    // (room icons, creatures, call-to-arms circles).  Uses the same rawblit shader
    // (palette_blit_vert + rawimage_blit_frag — opaque) and VAO/VBO layout, scaled
    // to the map_area dest rect supplied by draw_overhead_map().
    // Multiple commands allowed per frame (e.g. full map + zoom box).
    for (const OverheadMapCmd& cmd : m_rt_overhead_map_cmds)
    {
        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Unit 0: RG8 tile data (R=value, G=operation)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_overhead_map_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (cmd.tiles_x != m_overhead_map_tex_w || cmd.tiles_y != m_overhead_map_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, cmd.tiles_x, cmd.tiles_y,
                         0, GL_RG, GL_UNSIGNED_BYTE, cmd.pixels.data());
            m_overhead_map_tex_w = cmd.tiles_x;
            m_overhead_map_tex_h = cmd.tiles_y;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.tiles_x, cmd.tiles_y,
                            GL_RG, GL_UNSIGNED_BYTE, cmd.pixels.data());
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

        // Unit 1: palette
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        // Unit 2: fade/ghost table — always bind a valid texture; fall back to
        // m_tex_null (1×1 zero) if the fade table is not yet loaded so the ghost
        // sampler never reads from an incomplete texture unit.
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_texFade ? m_texFade : m_tex_null);

        // Unit 3: parchment background (reuse rawblit tex — contains the last
        // uploaded parchment image which is still valid on the GPU)
        if (m_rawblit_tex) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
        }

        const float sw = (float)m_rt_frame_state.screen_w;
        const float sh = (float)m_rt_frame_state.screen_h;
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        GLuint prog = m_overhead_map_shader ? m_overhead_map_shader : m_rawblit_shader;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog);

        // Set per-draw uniforms for the ghost-table shader
        if (m_overhead_map_shader && prog == m_overhead_map_shader) {
            if (m_omap_loc_screen_size >= 0)
                glUniform2f(m_omap_loc_screen_size, sw, sh);
            if (m_omap_loc_map_rect >= 0)
                glUniform4f(m_omap_loc_map_rect,
                            (float)cmd.dst_x, (float)cmd.dst_y,
                            (float)(cmd.dst_x + cmd.dst_w), (float)(cmd.dst_y + cmd.dst_h));
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
    }
    m_rt_overhead_map_cmds.clear();

    // ── PiP isometric render (ZBM_ISOMETRIC zoom-box mode) ────────────────
    // Each entry in m_rt_pip_queue is rendered into its own FBO slot (grown on
    // demand) then submitted to GLUIRenderer for compositing.  Queue cleared.
    if (m_world_renderer && !m_rt_pip_queue.empty())
    {
        IUIRenderer* ui = RendererGetUIRenderer();

        for (std::size_t qi = 0; qi < m_rt_pip_queue.size(); ++qi)
        {
            const PiPCmd& pcmd = m_rt_pip_queue[qi];
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

            const int32_t saved_vw = vec_window_width;
            const int32_t saved_vh = vec_window_height;
            vec_window_width  = pw;
            vec_window_height = ph;

            TbGraphicsWindow saved_ewnd;
            store_engine_window(&saved_ewnd, 1);
            setup_engine_window(0, 0, pw, ph);

            if (ui)
                ui->BeginPiPSprites();

            m_world_renderer->BeginPiPCapture();
            m_world_renderer->BeginWorldPass(nullptr, 0, pw, ph, 0, 0);

            // Save projection globals so main-view mouse→world unprojection is unaffected.
            const Offset saved_vert[3] = { vert_offset[0], vert_offset[1], vert_offset[2] };
            const Offset saved_hori[3] = { hori_offset[0], hori_offset[1], hori_offset[2] };
            const int32_t saved_x_init  = x_init_off;
            const int32_t saved_y_init  = y_init_off;

            draw_view(const_cast<Camera*>(&pcmd.cam_copy), 0);

            vert_offset[0] = saved_vert[0]; vert_offset[1] = saved_vert[1]; vert_offset[2] = saved_vert[2];
            hori_offset[0] = saved_hori[0]; hori_offset[1] = saved_hori[1]; hori_offset[2] = saved_hori[2];
            x_init_off = saved_x_init;
            y_init_off = saved_y_init;

            setup_engine_window(saved_ewnd.x, saved_ewnd.y,
                                saved_ewnd.width, saved_ewnd.height);
            vec_window_width  = saved_vw;
            vec_window_height = saved_vh;

            glDisable(GL_SCISSOR_TEST);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo.fbo);
            glViewport(0, 0, pw, ph);
            glClearColor(KFX_GL_CLEAR_COLOR);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            m_world_renderer->GPURenderToFBO(pw, ph);
            if (ui)
                ui->DrawPiPSprites(pw, ph);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);

            if (ui)
                ui->SubmitFBOQuad(pcmd.x, pcmd.y, pw, ph, static_cast<GpuTextureHandle>(fbo.color_tex), pcmd.clip_radius);
        }

        m_rt_pip_queue.clear();
    }
    // Uses the same palette-indexed quad shader as rawblit, but with a per-video-frame
    // palette texture on unit 1 instead of the game palette.
    // The glClear() at the top of EndFrame already fills letterbox areas with black.
    if (m_rt_fmv_pending)
    {
        const FmvBlitCmd& cmd = m_rt_fmv_cmd;

        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
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
        const float sw = (float)m_rt_frame_state.screen_w;
        const float sh = (float)m_rt_frame_state.screen_h;
        Vec2f ndc0 = ScreenToNDC((float)cmd.dst_x,              (float)cmd.dst_y,              sw, sh);
        Vec2f ndc1 = ScreenToNDC((float)(cmd.dst_x + cmd.dst_w), (float)(cmd.dst_y + cmd.dst_h), sw, sh);
        const float verts[6][4] = {
            { ndc0.x, ndc1.y,  0.f, 1.f },
            { ndc1.x, ndc1.y,  1.f, 1.f },
            { ndc1.x, ndc0.y,  1.f, 0.f },
            { ndc0.x, ndc1.y,  0.f, 1.f },
            { ndc1.x, ndc0.y,  1.f, 0.f },
            { ndc0.x, ndc0.y,  0.f, 0.f },
        };
        glBindVertexArray(m_fmv_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_fmv_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_rt_fmv_pending = false;
    }

    // Landview zoom GPU blit — campaign-map zoom transition.
    // Draws map_screen as a fullscreen opaque quad; the fragment shader computes
    // each pixel's source texel from gl_FragCoord + per-frame zoom uniforms,
    // exactly matching frontzoom_to_point() arithmetic.
    // The transparent overlay (m_rt_transparent_blit_pending) composites compressed_window_draw()
    // output on top with index-0-transparent blending after this pass.
    if (m_rt_zoom_pending)
    {
        const LandviewZoomCmd& cmd = m_rt_zoom_cmd;

        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(fs_quad), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fs_quad), fs_quad);

        glUseProgram(m_zoom_shader);
        glUniform2f(m_zoom_u_center_map, cmd.center_map_x, cmd.center_map_y);
        glUniform2f(m_zoom_u_screen_ctr, cmd.screen_cx,    cmd.screen_cy);
        glUniform1f(m_zoom_u_scale,      cmd.scale);
        glUniform2f(m_zoom_u_inv_map_sz, 1.0f / (float)cmd.src_w,
                                         1.0f / (float)cmd.src_h);
        glUniform1f(m_zoom_u_screen_h,   (float)m_rt_frame_state.screen_h);

        glDisable(GL_BLEND);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_rt_zoom_pending = false;
    }

    // Transparent overlay blit — composites SubmitTransparentBlit() data over the GPU
    // frame with index-0 transparency.  Used for the landview window frame.
    // Upload the CPU buffer to m_texIndex here on the render thread.
    if (m_rt_transparent_blit_pending)
    {
        if (!m_rt_transparent_blit_buf.empty())
        {
            glBindTexture(GL_TEXTURE_2D, m_texIndex);
            const int tbw = m_rt_frame_state.screen_w;
            const int tbh = m_rt_frame_state.screen_h;
            if (tbw != m_texIndex_w || tbh != m_texIndex_h)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tbw, tbh, 0, GL_RED, GL_UNSIGNED_BYTE,
                             m_rt_transparent_blit_buf.data());
                m_texIndex_w = tbw;
                m_texIndex_h = tbh;
            }
            else
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tbw, tbh,
                                GL_RED, GL_UNSIGNED_BYTE, m_rt_transparent_blit_buf.data());
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            m_rt_transparent_blit_buf.clear();
        }

        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texIndex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_shader);
        glUniform1f(m_uTintFactor, m_rt_frame_state.possession_tint);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        m_rt_transparent_blit_pending = false;
    }

    // Draw layer-1 (front) GPU UI elements first.
    // Layer-1 flush: FBO quads (PiP), atlas sprites, minimap — everything except
    // layer-2/3 (world-depth overlays, top-overlay tooltip/zoom-box corners).
    {
        IUIRenderer* ui = RendererGetUIRenderer();
        if (ui) ui->DrawFrontBase();
    }

    // ── Zoom-box tile quads (ZBM_OVERHEAD with actual tile textures) ───────
    // Drawn AFTER layer-1 sprites so the tile render lands on top of all
    // parchment-map UI sprites (overhead creatures, room icons, call-to-arms).
    // The layer-3 top-overlay (tooltip, corner frames) will follow in
    // DrawFrontOverlay() below, so they are unaffected.
    // Step 1: fill each zoom box region with solid black so unrevealed tiles
    //         and skipped rock tiles appear black rather than showing whatever
    //         is underneath (overhead map, parchment).
    if (!m_rt_zoom_box_bg_cmds.empty())
    {
        glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        if (m_tintProg)
        {
            static const float k_black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            glUseProgram(m_tintProg);
            glUniform4fv(m_uTintColor, 1, k_black);
            glUniform1f(m_uTintClipScrH, (float)m_rt_frame_state.screen_h);
            glBindVertexArray(m_rawblit_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
            const float sw = (float)m_rt_frame_state.screen_w;
            const float sh = (float)m_rt_frame_state.screen_h;
            for (const ZoomBoxBgCmd& bg : m_rt_zoom_box_bg_cmds)
            {
                glUniform1f(m_uTintClipRadius, bg.clip_radius);
                if (bg.clip_radius >= 0.0f)
                {
                    float cr[4] = { (float)bg.x, (float)bg.y,
                                    (float)(bg.x + bg.w), (float)(bg.y + bg.h) };
                    glUniform4fv(m_uTintClipRect, 1, cr);
                }
                Vec2f ndc0 = ScreenToNDC((float)bg.x,          (float)bg.y,          sw, sh);
                Vec2f ndc1 = ScreenToNDC((float)(bg.x + bg.w),  (float)(bg.y + bg.h), sw, sh);
                const float verts[6][4] = {
                    { ndc0.x, ndc1.y,  0.f, 0.f },
                    { ndc1.x, ndc1.y,  0.f, 0.f },
                    { ndc1.x, ndc0.y,  0.f, 0.f },
                    { ndc0.x, ndc1.y,  0.f, 0.f },
                    { ndc1.x, ndc0.y,  0.f, 0.f },
                    { ndc0.x, ndc0.y,  0.f, 0.f },
                };
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glBindVertexArray(0);
        }
        else
        {
            float saved_cc[4];
            glGetFloatv(GL_COLOR_CLEAR_VALUE, saved_cc);
            glClearColor(KFX_GL_CLEAR_COLOR);
            glEnable(GL_SCISSOR_TEST);
            for (const ZoomBoxBgCmd& bg : m_rt_zoom_box_bg_cmds)
            {
                int gl_y = m_rt_frame_state.screen_h - (bg.y + bg.h);
                glScissor(bg.x, gl_y, bg.w, bg.h);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glDisable(GL_SCISSOR_TEST);
            glClearColor(saved_cc[0], saved_cc[1], saved_cc[2], saved_cc[3]);
        }
        glDepthMask(GL_TRUE);
    }
    m_rt_zoom_box_bg_cmds.clear();

    // Step 2: draw textured tile quads on top of the black background.
    if (!m_rt_zoom_tile_cmds.empty() && m_zoom_tile_shader && m_tile_atlas)
    {
        GLuint atlas_tex = m_tile_atlas->GetAtlasTexture(0);
        if (atlas_tex)
        {
            glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_texPalette);

            glUseProgram(m_zoom_tile_shader);
            glUniform1f(m_uZoomClipRadius, m_rt_zoom_clip_radius);
            glUniform1f(m_uZoomClipScrH, (float)m_rt_frame_state.screen_h);
            if (m_rt_zoom_clip_radius >= 0.0f)
                glUniform4fv(m_uZoomClipRect, 1, m_rt_zoom_clip_rect);
            glBindVertexArray(m_rawblit_vao);
            glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);

            const float sw = (float)m_rt_frame_state.screen_w;
            const float sh = (float)m_rt_frame_state.screen_h;

            for (const ZoomTileCmd& c : m_rt_zoom_tile_cmds)
            {
                Vec2f ndc0 = ScreenToNDC(c.dst_x,             c.dst_y,             sw, sh);
                Vec2f ndc1 = ScreenToNDC(c.dst_x + c.dst_w,   c.dst_y + c.dst_h,   sw, sh);
                const float verts[6][4] = {
                    { ndc0.x, ndc1.y,  c.u0, c.v1 },
                    { ndc1.x, ndc1.y,  c.u1, c.v1 },
                    { ndc1.x, ndc0.y,  c.u1, c.v0 },
                    { ndc0.x, ndc1.y,  c.u0, c.v1 },
                    { ndc1.x, ndc0.y,  c.u1, c.v0 },
                    { ndc0.x, ndc0.y,  c.u0, c.v0 },
                };
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }

            glBindVertexArray(0);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            glActiveTexture(GL_TEXTURE0);
        }
    }
    m_rt_zoom_tile_cmds.clear();

    // Layer-2/3 overlay: world-depth sprites and top-overlay (tooltip, corner frames).
    // Software UIRenderer: DrawFrontBase() (above) already drew everything, so
    // DrawFrontOverlay() is a no-op on the base interface.
    {
        IUIRenderer* ui = RendererGetUIRenderer();
        if (ui) ui->DrawFrontOverlay();
    }

    // Text on top of all sprites (sidebar labels, event messages, tooltips).
    // Software ITextRenderer: ExecuteTextFromIR() default calls Draw() so the
    // fallback path is handled automatically without a cast or else branch.
    if (m_textRenderer)
        m_textRenderer->ExecuteTextFromIR(m_render_graph.GetTextBuffersRT());

    if (m_rt_frame_state.screen_tint[3] > 0.0f && m_tintProg)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_tintProg);
        glUniform4fv(m_uTintColor, 1, m_rt_frame_state.screen_tint);
        glUniform1f(m_uTintClipRadius, -1.0f);  // no clip for fullscreen tint
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    SYNCDBG(0, "EndFrame_GL step 5: before cursor draw");
    if (auto* cursor = RendererGetCursorLayer())
        cursor->ExecuteCursorFromIR(m_render_graph.GetUIBuffersRT());

    // Screenshot capture: after all draw calls, before buffer swap so that the
    // default framebuffer holds the fully-composited frame.
    if (!m_rt_screenshot_path.empty())
    {
        const int w = m_rt_frame_state.screen_w;
        const int h = m_rt_frame_state.screen_h;
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // GL origin is bottom-left; flip rows for top-down image formats.
        std::vector<uint8_t> flipped(pixels.size());
        const size_t row_bytes = static_cast<size_t>(w) * 4;
        for (int row = 0; row < h; ++row)
            std::memcpy(flipped.data() + row * row_bytes,
                        pixels.data() + (h - 1 - row) * row_bytes,
                        row_bytes);
        RendererHelper_SaveRGBAImage(flipped.data(), w, h, static_cast<int>(row_bytes),
                                     m_rt_screenshot_fmt, m_rt_screenshot_path.c_str());
        m_rt_screenshot_path.clear();
    }

    platform_swap_gl_buffers(platform_get_sdl_window());

    KFX_GPU_COLLECT();
    KFX_FRAMEMARK();
}

uint8_t* RendererOpenGL::LockFramebuffer(int* out_pitch)
{
    // GPU mode: no CPU framebuffer. RendererLockScreen() handles the GPU branch
    // directly and never calls this function; returning null is a safe fallback.
    if (out_pitch) *out_pitch = 0;
    return nullptr;
}

void RendererOpenGL::UnlockFramebuffer()
{
    // No-op in GL mode — discard buffer writes are silently ignored.
}

bool RendererOpenGL::BlitRaw8GPU(int dst_width, int dst_height, int dst_x, int dst_y,
                                  const unsigned char* src_buf, int src_width, int src_height)
{
    if (!src_buf || src_width <= 0 || src_height <= 0)
    {
        ERRORLOG("RendererOpenGL::BlitRaw8GPU: invalid source (buf=%p w=%d h=%d)",
                 (const void*)src_buf, src_width, src_height);
        return false;
    }
    // Copy pixel data into an owned buffer so the caller may free its buffer as
    // soon as this function returns — before the render thread reads.
    m_rawblit_px_buf.assign(src_buf, src_buf + (size_t)src_width * src_height);
    m_rawblit_cmd            = { m_rawblit_px_buf.data(), src_width, src_height,
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
    if (!px || !bgra_pal || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        ERRORLOG("RendererOpenGL::SubmitVideoFrame: invalid params (px=%p src=%dx%d dst=%dx%d)",
                 (const void*)px, src_w, src_h, dst_w, dst_h);
        return false;
    }
    // Copy pixel data and palette into owned buffers so the caller (AVFrame) may
    // be freed as soon as this function returns — before the render thread reads.
    m_fmv_px_buf.assign(px, px + (size_t)src_h * src_pitch);
    m_fmv_pal_buf.assign(bgra_pal, bgra_pal + 256 * 4);
    m_fmv_cmd = { m_fmv_px_buf.data(), src_w, src_h, src_pitch,
                  m_fmv_pal_buf.data(), dst_x, dst_y, dst_w, dst_h };
    m_fmv_pending = true;
    return true;
}

bool RendererOpenGL::SubmitLandviewZoom(
    const uint8_t* src_buf, int src_w, int src_h,
    float center_map_x, float center_map_y,
    float screen_cx,    float screen_cy,
    float scale)
{
    if (!src_buf || src_w <= 0 || src_h <= 0)
    {
        ERRORLOG("RendererOpenGL::SubmitLandviewZoom: invalid params (src=%p %dx%d)",
                 (const void*)src_buf, src_w, src_h);
        return false;
    }
    // Copy into an owned buffer so map_screen may be written freely by the game
    // thread once this call returns, before the render thread reads.
    m_zoom_src_buf.assign(src_buf, src_buf + (size_t)src_w * src_h);
    m_zoom_cmd = { m_zoom_src_buf.data(), src_w, src_h,
                   center_map_x, center_map_y,
                   screen_cx, screen_cy, scale };
    m_zoom_pending = true;
    return true;
}

bool RendererOpenGL::SubmitTransparentBlit(const uint8_t* buf, int w, int h)
{
    if (w != m_screenW || h != m_screenH)
        return false;
    // Copy into CPU buffer; the render thread uploads to m_texIndex in EndFrame_GL()
    // / FlushSceneToFBO() so glTexSubImage2D never runs on the game thread.
    m_transparent_blit_buf.assign(buf, buf + (size_t)w * h);
    m_transparent_blit_pending = true;
    return true;
}

void RendererOpenGL::DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                                     bool draw_lr, int engine_window_x)
{
    if (!m_sprite_atlas)
        return;

    static const int SPRITES_X = 3;
    static const int SPRITES_Y = 2;

    const struct TbSprite* sprlist = get_sprite(sprites, SPRITES_X * SPRITES_Y * frame);
    if (!sprlist)
        return;

    int scr_w = m_screenW;
    int scr_h = m_screenH;

    // Compute the sprite layout (positions + scale)
    const struct TbSprite* startspr = &sprlist[1];
    const struct TbSprite* endspr   = &sprlist[1];
    long allwidth = 0;
    for (int n = 0; n < SPRITES_X; n++)
    {
        allwidth += endspr->SWidth;
        endspr++;
    }
    int units_per_px = (RendererPhysicalWidth() * 59 / 64) * 16 / allwidth;
    int scrpos_y = (scr_h * 16 / units_per_px - (startspr->SHeight + endspr->SHeight)) / 2;
    const float alpha = 0.333f; // Lb_SPRITE_TRANSPAR4

    // Record sprite quads as raw vertex data for FlushSwipeQuads().
    auto push_quad = [&](float px, float py, float pw, float ph,
                         float u0, float v0, float u1, float v1)
    {
        // Two triangles: TL, TR, BR, TL, BR, BL
        m_swipe_verts.push_back({px,      py,      u0, v0, 1,1,1, alpha});
        m_swipe_verts.push_back({px + pw, py,      u1, v0, 1,1,1, alpha});
        m_swipe_verts.push_back({px + pw, py + ph, u1, v1, 1,1,1, alpha});
        m_swipe_verts.push_back({px,      py,      u0, v0, 1,1,1, alpha});
        m_swipe_verts.push_back({px + pw, py + ph, u1, v1, 1,1,1, alpha});
        m_swipe_verts.push_back({px,      py + ph, u0, v1, 1,1,1, alpha});
    };

    if (draw_lr)
    {
        int delta_y = sprlist[1].SHeight;
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &startspr[i];
            int scrpos_x = ((scr_w + (2 * engine_window_x)) * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                SpriteUV uv;
                if (m_sprite_atlas->GetUV(spr, uv))
                {
                    float pw = (float)((uv.pixel_w * units_per_px + 8) / 16);
                    float ph = (float)((uv.pixel_h * units_per_px + 8) / 16);
                    float px = (float)(scrpos_x * units_per_px / 16);
                    float py = (float)(scrpos_y * units_per_px / 16);
                    push_quad(px, py, pw, ph, uv.u0, uv.v0, uv.u1, uv.v1);
                }
                scrpos_x += spr->SWidth;
                spr++;
            }
            scrpos_y += delta_y;
        }
    }
    else
    {
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &sprlist[SPRITES_X + i];
            int delta_y = spr->SHeight;
            int scrpos_x = (scr_w * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                SpriteUV uv;
                if (m_sprite_atlas->GetUV(spr, uv))
                {
                    float pw = (float)((uv.pixel_w * units_per_px + 8) / 16);
                    float ph = (float)((uv.pixel_h * units_per_px + 8) / 16);
                    float px = (float)(scrpos_x * units_per_px / 16);
                    float py = (float)(scrpos_y * units_per_px / 16);
                    // flip_horiz: swap u0/u1
                    push_quad(px, py, pw, ph, uv.u1, uv.v0, uv.u0, uv.v1);
                }
                scrpos_x += spr->SWidth;
                spr--;
            }
            scrpos_y += delta_y;
        }
    }
}

void RendererOpenGL::FlushSwipeQuads()
{
    if (m_rt_swipe_verts.empty() || !m_sprite_atlas)
        return;

    // Lazy-init shader + VAO/VBO on first use
    if (!m_swipe_shader)
    {
        unsigned int vs = compile_shader(GL_VERTEX_SHADER,   UI_VERTEX_SHADER);
        unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, UI_SPRITE_FRAGMENT_SHADER);
        if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); m_rt_swipe_verts.clear(); return; }
        m_swipe_shader = glCreateProgram();
        glAttachShader(m_swipe_shader, vs);
        glAttachShader(m_swipe_shader, fs);
        glLinkProgram(m_swipe_shader);
        glDeleteShader(vs);
        glDeleteShader(fs);
        int ok = 0;
        glGetProgramiv(m_swipe_shader, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(m_swipe_shader); m_swipe_shader = 0; m_rt_swipe_verts.clear(); return; }
        glUseProgram(m_swipe_shader);
        glUniform1i(glGetUniformLocation(m_swipe_shader, "u_sprite_atlas"), 0);
        glUniform1i(glGetUniformLocation(m_swipe_shader, "u_palette"),      1);

        glGenVertexArrays(1, &m_swipe_vao);
        glGenBuffers(1, &m_swipe_vbo);
        glBindVertexArray(m_swipe_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_swipe_vbo);
        // SwipeVertex: x, y, u, v, r, g, b, a (8 floats = 32 bytes)
        glEnableVertexAttribArray(0); // a_pos (vec2)
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SwipeVertex), (void*)0);
        glEnableVertexAttribArray(1); // a_uv (vec2)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SwipeVertex), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2); // a_color (vec4)
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(SwipeVertex), (void*)(4 * sizeof(float)));
        // Attributes 3 (a_z) and 4 (a_mode) unused — set constant
        glVertexAttrib1f(3, 0.5f);
        glVertexAttrib1f(4, 0.0f);
        glBindVertexArray(0);
    }

    glUseProgram(m_swipe_shader);
    glUniform2f(glGetUniformLocation(m_swipe_shader, "u_screen_size"),
                (float)m_rt_frame_state.screen_w, (float)m_rt_frame_state.screen_h);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sprite_atlas->GetTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texPalette);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_swipe_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_swipe_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(m_rt_swipe_verts.size() * sizeof(SwipeVertex)),
                 m_rt_swipe_verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_rt_swipe_verts.size());

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);

    m_rt_swipe_verts.clear();
}

/******************************************************************************/
// Lens Post-Process Infrastructure
/******************************************************************************/

static unsigned int create_rgba_texture(int w, int h, const char* label)
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
    (void)label;
    return tex;
}

static unsigned int create_fbo_with_texture(unsigned int color_tex, unsigned int depth_rb)
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

void RendererOpenGL::EnsureLensFBOs()
{
    if (m_lens_fbo_w == m_rt_frame_state.screen_w && m_lens_fbo_h == m_rt_frame_state.screen_h && m_lens_scene_fbo)
        return;

    // Free existing
    if (m_lens_scene_fbo)      { glDeleteFramebuffers(1, &m_lens_scene_fbo);       m_lens_scene_fbo = 0; }
    if (m_lens_scene_tex)      { glDeleteTextures(1, &m_lens_scene_tex);           m_lens_scene_tex = 0; }
    if (m_lens_scene_depth_rb) { glDeleteRenderbuffers(1, &m_lens_scene_depth_rb); m_lens_scene_depth_rb = 0; }
    if (m_lens_pass_fbo_a)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_a);      m_lens_pass_fbo_a = 0; }
    if (m_lens_pass_tex_a)     { glDeleteTextures(1, &m_lens_pass_tex_a);          m_lens_pass_tex_a = 0; }
    if (m_lens_pass_fbo_b)     { glDeleteFramebuffers(1, &m_lens_pass_fbo_b);      m_lens_pass_fbo_b = 0; }
    if (m_lens_pass_tex_b)     { glDeleteTextures(1, &m_lens_pass_tex_b);          m_lens_pass_tex_b = 0; }

    int w = m_rt_frame_state.screen_w;
    int h = m_rt_frame_state.screen_h;

    // Scene FBO — world renders here instead of default framebuffer
    m_lens_scene_tex = create_rgba_texture(w, h, "Lens/SceneTex");

    glGenRenderbuffers(1, &m_lens_scene_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_lens_scene_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    m_lens_scene_fbo = create_fbo_with_texture(m_lens_scene_tex, m_lens_scene_depth_rb);

    // Ping-pong FBOs (no depth needed — post-process only)
    m_lens_pass_tex_a = create_rgba_texture(w, h, "Lens/PassTexA");
    m_lens_pass_fbo_a = create_fbo_with_texture(m_lens_pass_tex_a, 0);

    m_lens_pass_tex_b = create_rgba_texture(w, h, "Lens/PassTexB");
    m_lens_pass_fbo_b = create_fbo_with_texture(m_lens_pass_tex_b, 0);

    m_lens_fbo_w = w;
    m_lens_fbo_h = h;

    // Compile passthrough shader (blit final texture to screen)
    if (!m_passthrough_shader)
    {
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
    }

    SYNCDBG(7, "Lens FBOs created: %dx%d (scene=%u passA=%u passB=%u)",
            w, h, m_lens_scene_fbo, m_lens_pass_fbo_a, m_lens_pass_fbo_b);
}

void RendererOpenGL::ApplyLensGPUPasses()
{
    LensManager* lm = LensManager::GetInstance();
    if (!lm || !lm->IsReady())
        return;

    // Collect active GPU passes
    std::vector<IPostProcessPass*> gpu_passes;
    for (LensEffect* e : lm->GetEffects())
    {
        if (e->IsEnabled())
        {
            IPostProcessPass* p = e->GetGPUPass();
            if (p) gpu_passes.push_back(p);
        }
    }

    if (gpu_passes.empty())
        return;

    EnsureLensFBOs();
    if (!m_lens_pass_fbo_a || !m_lens_pass_fbo_b)
        return;

    // Ping-pong: scene_tex → pass_a → pass_b → ...
    unsigned int src_tex = m_lens_scene_tex;
    bool flip = false;
    for (IPostProcessPass* pass : gpu_passes)
    {
        unsigned int dst_fbo = flip ? m_lens_pass_fbo_b : m_lens_pass_fbo_a;
        unsigned int dst_tex = flip ? m_lens_pass_tex_b : m_lens_pass_tex_a;
        pass->Apply(src_tex, dst_fbo, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
        src_tex = dst_tex;
        flip = !flip;
    }

    // Blit final result to default framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_rt_frame_state.screen_w, m_rt_frame_state.screen_h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);
    glUseProgram(m_passthrough_shader);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

bool RendererOpenGL::SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                                        int dst_x, int dst_y, int dst_w, int dst_h)
{
    OverheadMapCmd cmd;
    cmd.pixels.assign(tile_colors, tile_colors + (size_t)tiles_x * (size_t)tiles_y * 2);
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
    bg.clip_radius = RendererGetZoomBoxClipRadius();
    m_zoom_box_bg_cmds.push_back(bg);

    // Cache the clip rect and radius for the tile draw pass (bg_cmds are consumed first).
    m_zoom_clip_radius = bg.clip_radius;
    m_zoom_clip_rect[0] = (float)dst_x;
    m_zoom_clip_rect[1] = (float)dst_y;
    m_zoom_clip_rect[2] = (float)(dst_x + bg.w);
    m_zoom_clip_rect[3] = (float)(dst_y + bg.h);

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
    cmd.clip_radius = RendererGetZoomBoxClipRadius();
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

/******************************************************************************/

void RendererOpenGL::FlushSceneToFBO(int w, int h)
{
    // Caller must have bound the FBO, set viewport, and cleared already.
    // This method consumes the pending render queues into the bound target.
    // All game-side coordinates (rawblit dst, overhead map dst) are in logical
    // screen space (m_rt_frame_state.screen_w × m_rt_frame_state.screen_h).  The FBO viewport matches the
    // logical dimensions so NDC conversion uses the same coordinate system.

    // ── Palette ──────────────────────────────────────────────────────────
    upload_palette_texture();

    // ── 3D world geometry ────────────────────────────────────────────────
    if (m_world_renderer)
        m_world_renderer->GPURenderToFBO(w, h);

    // ── Raw blit (parchment background image) ────────────────────────────
    if (m_rt_rawblit_pending)
    {
        const RawBlitCmd& cmd = m_rt_rawblit_cmd;
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

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

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        // Coordinates are in logical screen space.
        const float sw = (float)m_rt_frame_state.screen_w;
        const float sh = (float)m_rt_frame_state.screen_h;
        Vec2f ndc0 = ScreenToNDC((float)cmd.dst_x,               (float)cmd.dst_y,               sw, sh);
        Vec2f ndc1 = ScreenToNDC((float)(cmd.dst_x + cmd.dst_w), (float)(cmd.dst_y + cmd.dst_h), sw, sh);

        const float verts[6][4] = {
            { ndc0.x, ndc1.y,  0.f, 1.f },
            { ndc1.x, ndc1.y,  1.f, 1.f },
            { ndc1.x, ndc0.y,  1.f, 0.f },
            { ndc0.x, ndc1.y,  0.f, 1.f },
            { ndc1.x, ndc0.y,  1.f, 0.f },
            { ndc0.x, ndc0.y,  0.f, 0.f },
        };
        glBindVertexArray(m_rawblit_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_rawblit_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        glDisable(GL_BLEND);
        glUseProgram(m_rawblit_shader);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
        m_rt_rawblit_pending = false;
    }

    // ── Overhead map tile blits ──────────────────────────────────────────
    for (const OverheadMapCmd& cmd : m_rt_overhead_map_cmds)
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Unit 0: RG8 tile data
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_overhead_map_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (cmd.tiles_x != m_overhead_map_tex_w || cmd.tiles_y != m_overhead_map_tex_h)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, cmd.tiles_x, cmd.tiles_y,
                         0, GL_RG, GL_UNSIGNED_BYTE, cmd.pixels.data());
            m_overhead_map_tex_w = cmd.tiles_x;
            m_overhead_map_tex_h = cmd.tiles_y;
        }
        else
        {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cmd.tiles_x, cmd.tiles_y,
                            GL_RG, GL_UNSIGNED_BYTE, cmd.pixels.data());
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        if (m_texFade) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_texFade);
        }
        if (m_rawblit_tex) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, m_rawblit_tex);
        }

        const float sw = (float)m_rt_frame_state.screen_w;
        const float sh = (float)m_rt_frame_state.screen_h;
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
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

        GLuint prog = m_overhead_map_shader ? m_overhead_map_shader : m_rawblit_shader;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog);

        if (m_overhead_map_shader && prog == m_overhead_map_shader) {
            if (m_omap_loc_screen_size >= 0)
                glUniform2f(m_omap_loc_screen_size, sw, sh);
            if (m_omap_loc_map_rect >= 0)
                glUniform4f(m_omap_loc_map_rect,
                            (float)cmd.dst_x, (float)cmd.dst_y,
                            (float)(cmd.dst_x + cmd.dst_w), (float)(cmd.dst_y + cmd.dst_h));
        }

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
    }
    m_rt_overhead_map_cmds.clear();

    // ── Transparent blit (CPU-drawn sprites composited over the scene) ───
    if (m_rt_transparent_blit_pending)
    {
        if (!m_rt_transparent_blit_buf.empty())
        {
            glBindTexture(GL_TEXTURE_2D, m_texIndex);
            const int tbw = m_rt_frame_state.screen_w;
            const int tbh = m_rt_frame_state.screen_h;
            if (tbw != m_texIndex_w || tbh != m_texIndex_h)
            {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tbw, tbh, 0, GL_RED, GL_UNSIGNED_BYTE,
                             m_rt_transparent_blit_buf.data());
                m_texIndex_w = tbw;
                m_texIndex_h = tbh;
            }
            else
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tbw, tbh,
                                GL_RED, GL_UNSIGNED_BYTE, m_rt_transparent_blit_buf.data());
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            m_rt_transparent_blit_buf.clear();
        }

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_texIndex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texPalette);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(m_shader);
        glUniform1f(m_uTintFactor, 0.0f);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glActiveTexture(GL_TEXTURE0);
        glUseProgram(0);
        m_rt_transparent_blit_pending = false;
    }
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
    unsigned int vert = compile_shader(GL_VERTEX_SHADER,   PALETTE_BLIT_VERTEX_SHADER);
    unsigned int frag = compile_shader(GL_FRAGMENT_SHADER, PALETTE_BLIT_FRAGMENT_SHADER);
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
        std::string tv_src = SCREEN_TINT_VERTEX_SHADER;
        std::string tf_src = SCREEN_TINT_FRAGMENT_SHADER;
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
                m_uTintClipRect   = glGetUniformLocation(m_tintProg, "u_clip_rect");
                m_uTintClipRadius = glGetUniformLocation(m_tintProg, "u_clip_radius");
                m_uTintClipScrH   = glGetUniformLocation(m_tintProg, "u_clip_screen_h");
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

IWorldViewRenderer* RendererOpenGL::CreateGLWorldViewRenderer()
{
    auto* glwr = new GLWorldViewRenderer(m_tile_atlas, m_texFade, m_texPalette);
    glwr->SetPaletteSource(lbPalette);
    SetWorldRenderer(glwr);
    return glwr;
}

IMapFadePass* RendererOpenGL::CreateGLMapFadePass()
{
    auto* glmf = new GLMapFadePass{this};
    glmf->SetScreenSize(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
    SetGLMapFadePass(glmf);
    return glmf;
}

ITextRenderer* RendererOpenGL::CreateGLTextRenderer()
{
    auto* glt = new GLTextRenderer();
    if (!glt->Init())
    {
        WARNLOG("GLTextRenderer::Init() failed");
        delete glt;
        return nullptr;
    }
    glt->SetPaletteTexture(m_texPalette);
    glt->SetScreenSize(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
    SetTextRenderer(glt);
    return glt;
}

IUIRenderer* RendererOpenGL::CreateGLUIRenderer()
{
    auto* glui = new GLUIRenderer();
    if (!glui->Init())
    {
        WARNLOG("GLUIRenderer::Init() failed, falling back to software");
        delete glui;
        return nullptr;
    }
    glui->SetSpriteAtlas(m_sprite_atlas);
    glui->SetFontAtlas(m_font_atlas);
    glui->SetPaletteTexture(m_texPalette, GL_TEXTURE_2D);
    glui->SetPaletteSource(lbPalette);
    glui->SetScreenDimensions(lbDisplay.PhysicalScreenWidth, lbDisplay.PhysicalScreenHeight);
    SetGLUIRenderer(glui);
    return glui;
}

ICursorLayer* RendererOpenGL::CreateGLCursorLayer()
{
    auto* glcur = new GLCursorLayer();
    glcur->SetWorldViewRenderer(m_world_renderer);
    glcur->SetSpriteAtlas(m_sprite_atlas);
    glcur->SetGLUIRenderer(m_gl_ui_renderer);
    return glcur;
}

bool RendererOpenGL::CompileSubRendererShaders()
{
    // All four GL sub-renderers implement IGLShaderCompilable via multiple
    // inheritance.  Typed pointers avoid dynamic_cast — each pointer is null
    // only if the sub-renderer fell back to a software implementation.
    IGLShaderCompilable* compilables[] = {
        m_world_renderer,     // GLWorldViewRenderer* — always set in GL path
        m_gl_mapfade,         // GLMapFadePass*       — always set in GL path
        m_textRenderer,       // GLTextRenderer*      — null if Init() failed
        m_gl_ui_renderer,     // GLUIRenderer*        — null if Init() failed
    };
    for (IGLShaderCompilable* c : compilables)
    {
        if (!c) continue;
        if (!c->CompileShaders())
        {
            ERRORLOG("RendererOpenGL: %s::CompileShaders() failed", c->RendererName());
            return false;
        }
    }
    return true;
}

void RendererOpenGL::upload_palette_texture()
{
    // Read from the per-frame snapshot — not from live lbPalette — so the render
    // thread never races against a concurrent LbPaletteSet() on the game thread.
    // Use m_palette_upload_buf (member, heap-allocated) rather than a local stack
    // array.  NVIDIA's Threaded Optimisation can defer reading the glTexSubImage2D
    // pixel pointer past the function return; a stack buffer would be recycled by
    // then.  The member buffer lives for the lifetime of the renderer.
    for (int i = 0; i < 256; ++i)
    {
        m_palette_upload_buf[i * 4 + 0] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 0] << 2);
        m_palette_upload_buf[i * 4 + 1] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 1] << 2);
        m_palette_upload_buf[i * 4 + 2] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 2] << 2);
        m_palette_upload_buf[i * 4 + 3] = 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_texPalette);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, m_palette_upload_buf);
    glActiveTexture(GL_TEXTURE0);
}

bool RendererOpenGL::init_fade_table_texture()
{
    if (!render_fade_tables)
    {
        ERRORLOG("init_fade_table_texture called but render_fade_tables is NULL");
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

void RendererOpenGL::NotifyTexturesReloaded()
{
    InvalidateTileAtlas();
}

void RendererOpenGL::NotifyGameTablesReady()
{
    // Schedule the fade-table GL texture for creation on the render thread.
    // init_fade_table_texture() issues glGenTextures/glTexImage2D; these must
    // run on the thread that owns the GL context.
    ScheduleFadeTableInit();

    // Wire the palette source pointer into sub-renderers that cache it.
    // SetPaletteSource is just a pointer copy — safe on the game thread.
    if (auto* ui = RendererGetUIRenderer())
        ui->SetPaletteSource(lbPalette);
    if (auto* wr = RendererGetWorldViewRenderer())
        wr->SetPaletteSource(lbPalette);
}

bool RendererOpenGL::ScheduleScreenshot(const char* path, int fmt)
{
    m_pending_screenshot_path = path ? path : "";
    m_pending_screenshot_fmt  = fmt;
    return true;  // optimistic — render thread will log errors on failure
}
