/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLWorldViewRenderer.cpp
 *     Desktop OpenGL world-geometry renderer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLWorldViewRenderer.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLTileAtlas.h"
#include "renderer/opengl/GLShaders.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/ITileAtlas.h"
#include "renderer/RendererManager.h"  // for RendererGetActive/RendererGetActiveType
#include "renderer/RendererSettings.h" // g_renderer_settings (transpar4_alpha, transpar8_alpha)
#include "renderer/RendererOpenGL.h"   // for RendererOpenGL class
#include "renderer/VecMath.h"

#include "engine_buckets.h"   // QKinds enum, BasicQ, BucketKind* structs, buckets[]
#include "engine_textures.h"  // TEXTURE_BLOCKS_COUNT
#include "renderer/TileAtlasPacker.h" // GetTileUV
#include "engine_render.h"    // draw_3d_sprites_for_bucket(), draw_nonspatial_sprites_no_shadows()
#include "bflib_render.h"      // PolyPoint, render_fade_tables
#include "bflib_vidraw.h"      // vec_window_width, vec_window_height
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG / WARNLOG
#include "renderer/opengl/GLUIRenderer.h"
#include "creature_graphics.h" // KeeperSprite structure
#include "bflib_sprite.h"      // TbSprite structure
#include "player_data.h"       // get_player_color_idx(), player_room_colours[]
#include "game_legacy.h"       // game.lish.subtile_lightness (lightmap snapshot in FlipBuffers)

#include <glad/glad.h>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "kfx/profiling/KfxProfiling.h"
#include "platform.h"
#include "post_inc.h"

/******************************************************************************/

static GLuint compile_shader_src(GLenum type, const char* src, const char* debug_name)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        ERRORLOG("GLWorldViewRenderer: shader '%s' compile error: %s", debug_name, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/******************************************************************************/

/** Max dimension of a keeper-sprite decode buffer (stride for GL_UNPACK_ROW_LENGTH). */
static constexpr int k_kspr_decode_dim = 256;

/** Scratch buffer used by render_keepersprite_gpu() to hold decoded palette indices.
 *  Stride is always k_kspr_decode_dim so glTexSubImage2D can use GL_UNPACK_ROW_LENGTH. */
static uint8_t s_kspr_decode_buf[k_kspr_decode_dim * k_kspr_decode_dim];

/** Decode keeper-sprite RLE into a stride-k_kspr_decode_dim palette-index buffer.
 *  Format is identical to TbSprite.Data: negative cmd = transparent skip,
 *  positive cmd = run of palette bytes, 0 = end of row. */
static void decode_keeper_rle(uint8_t* dst, const uint8_t* data, int w, int h)
{
    if (!data || w <= 0 || h <= 0) return;

    for (int y = 0; y < h; ++y)
        memset(dst + y * k_kspr_decode_dim, 0, k_kspr_decode_dim);

    // Upper-bound on bytes we'll consume: generous worst-case for valid RLE.
    const signed char* sp     = reinterpret_cast<const signed char*>(data);
    const signed char* sp_end = sp + (ptrdiff_t)w * h * 3 + h;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + y * k_kspr_decode_dim;
        int x = 0;
        while (true) {
            if (sp >= sp_end) {
                WARNLOG("decode_keeper_rle: ran past expected data end at row %d", y);
                return;
            }
            signed char cmd = *sp++;
            if (cmd == 0) break;
            if (cmd < 0) {
                x += (int)(-cmd);
            } else {
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
                    if (sp >= sp_end) {
                        WARNLOG("decode_keeper_rle: pixel data past expected end");
                        return;
                    }
                    if (x < w) row[x] = (uint8_t)(*sp);
                    ++sp;
                    ++x;
                }
            }
        }
    }
}

/******************************************************************************/

GLWorldViewRenderer::GLWorldViewRenderer(ITileAtlas* atlas,
                                         GLuint      fade_tex,
                                         GLuint      palette_tex)
    : m_atlas(atlas)
    , m_fade_tex(fade_tex)
    , m_palette_tex(palette_tex)
{
    init_gl_resources();
}

GLWorldViewRenderer::~GLWorldViewRenderer()
{
    free_gl_resources();
}

/******************************************************************************/

bool GLWorldViewRenderer::CompileShaders()
{
    // init_gl_resources() is idempotent (guarded by m_initialized).
    // It compiles all GLSL programs for world, shadow, keeper-sprite, and flat-poly passes.
    return init_gl_resources();
}

/******************************************************************************/

bool GLWorldViewRenderer::init_gl_resources()
{
    if (m_initialized)
        return true;

    SYNCLOG("GLWorldViewRenderer: init_gl_resources starting (RenderDoc=%d)",
            platform_is_renderdoc_present());

    if (!compile_world_shaders())
    {
        ERRORLOG("GLWorldViewRenderer: compile_world_shaders failed");
        return false;
    }

    // VAO + dynamic VBO for world geometry
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_vao, "WVR/WorldVAO");
    KFX_GL_LABEL(GL_BUFFER,       m_vbo, "WVR/WorldVBO");

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(k_max_verts * sizeof(WorldVertex)),
                 nullptr, GL_DYNAMIC_DRAW);

    // layout(location=0) vec3 a_pos  — x,y,z at byte offset 0
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)0);
    glEnableVertexAttribArray(0);
    // layout(location=1) vec2 a_uv   — u,v at byte offset 12 (after x,y,z)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // layout(location=2) float a_shade — shade at byte offset 20 (after x,y,z,u,v)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    // layout(location=3) vec2 a_stl — subtile coords at byte offset 24 (after x,y,z,u,v,shade)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    // layout(location=4) float a_camera_z — camera-space depth for perspective correction, byte offset 32
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(4);
    // layout(location=5) float a_layer — texture array layer (atlas variation), byte offset 36
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(5);

    glBindVertexArray(0);

    // CPU staging buffer for PiP capture
    m_pip_verts.reserve(k_max_verts);

    // Cache uniform locations and bind samplers to fixed texture units
    glUseProgram(m_shader);
    m_loc_tile_atlas = glGetUniformLocation(m_shader, "u_tile_atlas");
    glUniform1i(m_loc_tile_atlas, 0);   // GL_TEXTURE0 — R8 palette-index atlas array
    m_loc_palette = glGetUniformLocation(m_shader, "u_palette");
    glUniform1i(m_loc_palette, 1);      // GL_TEXTURE1 — 1D RGBA8 palette
    // Shade / lighting uniforms — cache locations and push defaults
    m_loc_fullbright    = glGetUniformLocation(m_shader, "u_fullbright");
    m_loc_ambient       = glGetUniformLocation(m_shader, "u_ambient");
    m_loc_shade_scale   = glGetUniformLocation(m_shader, "u_shade_scale");
    m_loc_shade_gamma   = glGetUniformLocation(m_shader, "u_shade_gamma");
    m_loc_lighting_mode = glGetUniformLocation(m_shader, "u_lighting_mode");
    m_loc_darkness_mode = glGetUniformLocation(m_shader, "u_darkness_mode");
    m_loc_fade_table    = glGetUniformLocation(m_shader, "u_fade_table");
    m_loc_time          = glGetUniformLocation(m_shader, "u_time");
    m_loc_fog_speed     = glGetUniformLocation(m_shader, "u_fog_speed");
    m_loc_fog_density   = glGetUniformLocation(m_shader, "u_fog_density");
    m_loc_lightmap      = glGetUniformLocation(m_shader, "u_lightmap");
    m_loc_missing_tile  = glGetUniformLocation(m_shader, "u_missing_tile");
    m_loc_tile_filter   = glGetUniformLocation(m_shader, "u_tile_filter");
    glUniform1f(m_loc_fullbright,    0.0f);
    glUniform1f(m_loc_ambient,       0.0f);
    glUniform1f(m_loc_shade_scale,   1.0f);
    glUniform1f(m_loc_shade_gamma,   1.0f);
    glUniform1i(m_loc_lighting_mode, RENDERER_LIGHTING_SOFTWARE);
    glUniform1i(m_loc_darkness_mode, RENDERER_DARKNESS_LINEAR);
    glUniform1i(m_loc_fade_table,    3);  // GL_TEXTURE3
    glUniform1f(m_loc_time,          0.0f);
    glUniform1f(m_loc_fog_speed,     1.0f);
    glUniform1f(m_loc_fog_density,   0.4f);
    glUniform1i(m_loc_lightmap,      2);  // GL_TEXTURE2
    glUniform1f(m_loc_missing_tile,  0.0f);
    glUniform1i(m_loc_tile_filter,   0);   // default: nearest
    m_tile_filter_applied = -1;  // force apply on first flush
    glUseProgram(0);

    // Phase 2: lightmap texture — mirrors game.lish.subtile_lightness[] each frame.
    // GL_R16UI stores the raw 0..16128 lightness values; the shader normalises them.
    glGenTextures(1, &m_tex_lightmap);
    KFX_GL_LABEL(GL_TEXTURE, m_tex_lightmap, "WVR/LightmapTex");
    glBindTexture(GL_TEXTURE_2D, m_tex_lightmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, MAX_SUBTILES_X, MAX_SUBTILES_Y,
                 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!init_shadow_shader())
    {
        ERRORLOG("GLWorldViewRenderer: failed to initialise shadow shader");
        free_gl_resources();
        return false;
    }

    if (!init_keeper_sprite_shader())
    {
        ERRORLOG("GLWorldViewRenderer: failed to initialise keeper-sprite shader");
        free_gl_resources();
        return false;
    }

    if (!init_flatpoly_shader())
    {
        ERRORLOG("GLWorldViewRenderer: failed to initialise flat-colour polygon shader");
        free_gl_resources();
        return false;
    }

    m_initialized = true;
    SYNCLOG("GLWorldViewRenderer: initialised (VBO %d verts × %zu bytes)",
            k_max_verts, sizeof(WorldVertex));
    return true;
}

void GLWorldViewRenderer::ClearKeeperSpriteAtlas()
{
    m_kspr_atlas_map.clear();
    m_kspr_atlas_used = 0;
    m_kspr_clut_map.clear();
    m_kspr_clut_used = 1;
    memset(m_kspr_clut_palette_snap, 0, sizeof(m_kspr_clut_palette_snap));
    SYNCDBG(6, "GLWorldViewRenderer: keeper-sprite atlas cleared");
}

void GLWorldViewRenderer::PreloadKeeperSpriteAtlas()
{
    // Push a CMD_PRELOAD_KSPR_ATLAS into the game-thread draw list.
    // It crosses FlipBuffers() safely (inside m_rt_draw_cmds) and is consumed
    // by execute_preload_atlas() on the render thread before any sprite draws.
    // This is the correct IR-consistent approach — no bare cross-thread booleans.
    DrawCmd cmd;
    cmd.type = DrawCmd::CMD_PRELOAD_KSPR_ATLAS;
    m_draw_cmds.push_back(cmd);
    SYNCLOG("GLWorldViewRenderer: CMD_PRELOAD_KSPR_ATLAS queued");
}

void GLWorldViewRenderer::execute_preload_atlas()
{
    if (!m_kspr_sprite_array || !m_kspr_atlas_shader) {
        ERRORLOG("execute_preload_atlas: GL resources not ready — preload skipped (sprite_array=%u, shader=%u)",
                 m_kspr_sprite_array, m_kspr_atlas_shader);
        return;
    }

    ensure_clut_valid();  // identity row 0 must exist before any atlas draws

    int preloaded = 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);

    // Vanilla sprites: only those already resident in RAM.
    // keepsprite[i] is lazy-loaded from disk; null entries haven't been
    // accessed yet and cannot be preloaded without a full disk-load pass.
    for (int i = 0; i < KEEPSPRITE_LENGTH && m_kspr_atlas_used < k_kspr_atlas_layers; i++)
    {
        if (!keepsprite[i] || !*keepsprite[i]) continue;
        if ((size_t)i >= creature_table_length) continue;
        const struct KeeperSprite& ks = creature_table[i];
        if (ks.SWidth <= 0 || ks.SHeight <= 0 ||
            ks.SWidth > k_kspr_decode_dim || ks.SHeight > k_kspr_decode_dim) continue;
        const uint8_t* data = *keepsprite[i];
        if (m_kspr_atlas_map.count(data)) continue;

        memset(s_kspr_decode_buf, 0, sizeof(s_kspr_decode_buf));
        decode_keeper_rle(s_kspr_decode_buf, data, ks.SWidth, ks.SHeight);
        int layer = m_kspr_atlas_used++;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, k_kspr_decode_dim);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                        ks.SWidth, k_kspr_decode_dim, 1,
                        GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_kspr_atlas_map[data] = {layer, ks.SWidth};
        preloaded++;
    }

    // Custom sprites: fully in RAM after init_custom_sprites().
    for (int i = 0; i < KEEPERSPRITE_ADD_NUM && m_kspr_atlas_used < k_kspr_atlas_layers; i++)
    {
        if (!keepersprite_add[i]) continue;
        const struct KeeperSprite& ks = creature_table_add[i];
        if (ks.SWidth <= 0 || ks.SHeight <= 0 ||
            ks.SWidth > k_kspr_decode_dim || ks.SHeight > k_kspr_decode_dim) continue;
        const uint8_t* data = keepersprite_add[i];
        if (m_kspr_atlas_map.count(data)) continue;

        memset(s_kspr_decode_buf, 0, sizeof(s_kspr_decode_buf));
        decode_keeper_rle(s_kspr_decode_buf, data, ks.SWidth, ks.SHeight);
        int layer = m_kspr_atlas_used++;
        glPixelStorei(GL_UNPACK_ROW_LENGTH, k_kspr_decode_dim);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                        ks.SWidth, k_kspr_decode_dim, 1,
                        GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        m_kspr_atlas_map[data] = {layer, ks.SWidth};
        preloaded++;
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    if (m_kspr_atlas_used > m_kspr_atlas_peak)
        m_kspr_atlas_peak = m_kspr_atlas_used;
    SYNCLOG("GLWorldViewRenderer: atlas preload complete — %d sprites uploaded (%d/%d layers used)",
            preloaded, m_kspr_atlas_used, k_kspr_atlas_layers);
}

void GLWorldViewRenderer::ensure_clut_valid()
{
    if (!m_kspr_clut_tex) return;
    if (memcmp(m_rt_palette, m_kspr_clut_palette_snap, sizeof(m_rt_palette)) == 0) return;

    memcpy(m_kspr_clut_palette_snap, m_rt_palette, sizeof(m_rt_palette));

    // Rebuild identity CLUT row 0: palette[i] for all i.
    // DK palette is 6-bit (0-63); shift left 2 to get 8-bit (0-252).
    // Index 0 = transparent (alpha 0); all others opaque.
    uint8_t row[256 * 4];
    for (int i = 0; i < 256; i++) {
        row[i*4+0] = (uint8_t)(m_rt_palette[i*3+0] << 2);
        row[i*4+1] = (uint8_t)(m_rt_palette[i*3+1] << 2);
        row[i*4+2] = (uint8_t)(m_rt_palette[i*3+2] << 2);
        row[i*4+3] = (i == 0) ? 0 : 255;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_kspr_clut_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);  // restore active unit

    // Invalidate all cached remap CLUTs — they depend on the old palette values.
    m_kspr_clut_map.clear();
    m_kspr_clut_used = 1;

    SYNCDBG(6, "GLWorldViewRenderer: CLUT rebuilt (palette changed)");
}

void GLWorldViewRenderer::free_gl_resources()
{
    m_pip_verts.clear();
    m_pip_verts.shrink_to_fit();

    if (m_vao)    { glDeleteVertexArrays(1, &m_vao);  m_vao = 0; }
    if (m_vbo)    { glDeleteBuffers(1, &m_vbo);        m_vbo = 0; }
    if (m_shader) { glDeleteProgram(m_shader);          m_shader = 0; }

    if (m_shadow_vao)             { glDeleteVertexArrays(1, &m_shadow_vao);            m_shadow_vao = 0; }
    if (m_shadow_vbo)             { glDeleteBuffers(1, &m_shadow_vbo);                 m_shadow_vbo = 0; }
    if (m_shadow_shader)          { glDeleteProgram(m_shadow_shader);                  m_shadow_shader = 0; }
    if (m_shadow_silhouette_tex)  { glDeleteTextures(1, &m_shadow_silhouette_tex);     m_shadow_silhouette_tex = 0; }

    if (m_kspr_vao)         { glDeleteVertexArrays(1, &m_kspr_vao);        m_kspr_vao = 0; }
    if (m_kspr_vbo)         { glDeleteBuffers(1, &m_kspr_vbo);              m_kspr_vbo = 0; }
    if (m_kspr_shader)      { glDeleteProgram(m_kspr_shader);               m_kspr_shader = 0; }
    if (m_kspr_glow_shader) { glDeleteProgram(m_kspr_glow_shader);          m_kspr_glow_shader = 0; }
    if (m_kspr_outline_shader)       { glDeleteProgram(m_kspr_outline_shader);       m_kspr_outline_shader = 0; }
    if (m_kspr_atlas_outline_shader) { glDeleteProgram(m_kspr_atlas_outline_shader); m_kspr_atlas_outline_shader = 0; }
    if (m_kspr_sprite_tex)  { glDeleteTextures(1, &m_kspr_sprite_tex);      m_kspr_sprite_tex = 0; }
    if (m_kspr_sprite_array){ glDeleteTextures(1, &m_kspr_sprite_array);    m_kspr_sprite_array = 0; }
    if (m_kspr_atlas_shader){ glDeleteProgram(m_kspr_atlas_shader);         m_kspr_atlas_shader = 0; }
    m_kspr_atlas_used = 0;
    m_kspr_atlas_map.clear();
    if (m_kspr_clut_tex)         { glDeleteTextures(1, &m_kspr_clut_tex);      m_kspr_clut_tex = 0; }
    if (m_kspr_atlas_glow_shader){ glDeleteProgram(m_kspr_atlas_glow_shader);  m_kspr_atlas_glow_shader = 0; }
    m_kspr_clut_map.clear();
    m_kspr_clut_used = 1;
    memset(m_kspr_clut_palette_snap, 0, sizeof(m_kspr_clut_palette_snap));

    if (m_flatpoly_vao)    { glDeleteVertexArrays(1, &m_flatpoly_vao); m_flatpoly_vao = 0; }
    if (m_flatpoly_vbo)    { glDeleteBuffers(1, &m_flatpoly_vbo);       m_flatpoly_vbo = 0; }
    if (m_flatpoly_shader) { glDeleteProgram(m_flatpoly_shader);        m_flatpoly_shader = 0; }
    if (m_tex_lightmap)    { glDeleteTextures(1, &m_tex_lightmap);      m_tex_lightmap = 0; }

    m_initialized = false;
}

bool GLWorldViewRenderer::compile_world_shaders()
{
    GLuint vert = compile_shader_src(GL_VERTEX_SHADER,   WORLD_VERTEX_SHADER,   "world_vert.glsl");
    GLuint frag = compile_shader_src(GL_FRAGMENT_SHADER, WORLD_FRAGMENT_SHADER, "world_frag.glsl");
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

    GLint linked = 0;
    glGetProgramiv(m_shader, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(m_shader, sizeof(log), nullptr, log);
        ERRORLOG("GLWorldViewRenderer: shader link error: %s", log);
        glDeleteProgram(m_shader);
        m_shader = 0;
        return false;
    }
    KFX_GL_LABEL(GL_PROGRAM, m_shader, "WVR/WorldProg");
    return true;
}

bool GLWorldViewRenderer::init_shadow_shader()
{
    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   SHADOW_VERTEX_SHADER, "shadow_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, SHADOW_FRAGMENT_SHADER, "shadow_frag.glsl");
    if (!sv || !sf)
    {
        if (sv) glDeleteShader(sv);
        if (sf) glDeleteShader(sf);
        return false;
    }
    m_shadow_shader = glCreateProgram();
    glAttachShader(m_shadow_shader, sv);
    glAttachShader(m_shadow_shader, sf);
    glLinkProgram(m_shadow_shader);
    glDeleteShader(sv);
    glDeleteShader(sf);

    GLint linked = 0;
    glGetProgramiv(m_shadow_shader, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(m_shadow_shader, sizeof(log), nullptr, log);
        ERRORLOG("GLWorldViewRenderer: shadow shader link error: %s", log);
        glDeleteProgram(m_shadow_shader);
        m_shadow_shader = 0;
        return false;
    }

    glUseProgram(m_shadow_shader);
    m_shadow_loc_viewport   = glGetUniformLocation(m_shadow_shader, "u_viewport");
    m_shadow_loc_darkness   = glGetUniformLocation(m_shadow_shader, "u_darkness");
    m_shadow_loc_silhouette = glGetUniformLocation(m_shadow_shader, "u_silhouette");
    m_shadow_loc_ndc_z      = glGetUniformLocation(m_shadow_shader, "u_ndc_z");
    glUniform1i(m_shadow_loc_silhouette, 0); // GL_TEXTURE0
    glUseProgram(0);

    KFX_GL_LABEL(GL_PROGRAM, m_shadow_shader, "WVR/ShadowProg");

    // Shadow quad VAO + VBO: 6 vertices × (vec2 pos + vec2 uv) = 4 floats each
    glGenVertexArrays(1, &m_shadow_vao);
    glGenBuffers(1, &m_shadow_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_shadow_vao, "WVR/ShadowVAO");
    KFX_GL_LABEL(GL_BUFFER,       m_shadow_vbo, "WVR/ShadowVBO");
    glBindVertexArray(m_shadow_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_shadow_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    // a_pos: layout location 0, vec2 at offset 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // a_uv: layout location 1, vec2 at offset 8
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Reusable 256x256 R8 silhouette texture — overwritten each frame per shadow
    glGenTextures(1, &m_shadow_silhouette_tex);
    KFX_GL_LABEL(GL_TEXTURE, m_shadow_silhouette_tex, "WVR/ShadowSilhouetteTex");
    glBindTexture(GL_TEXTURE_2D, m_shadow_silhouette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, k_kspr_decode_dim, k_kspr_decode_dim, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool GLWorldViewRenderer::init_keeper_sprite_shader()
{
    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,   "kspr_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, KSPR_FRAGMENT_SHADER, "kspr_frag.glsl");
    if (!sv || !sf)
    {
        if (sv) glDeleteShader(sv);
        if (sf) glDeleteShader(sf);
        return false;
    }
    m_kspr_shader = glCreateProgram();
    glAttachShader(m_kspr_shader, sv);
    glAttachShader(m_kspr_shader, sf);
    glLinkProgram(m_kspr_shader);
    glDeleteShader(sv);
    glDeleteShader(sf);

    GLint linked = 0;
    glGetProgramiv(m_kspr_shader, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(m_kspr_shader, sizeof(log), nullptr, log);
        ERRORLOG("GLWorldViewRenderer: keeper-sprite shader link error: %s", log);
        glDeleteProgram(m_kspr_shader);
        m_kspr_shader = 0;
        return false;
    }

    glUseProgram(m_kspr_shader);
    m_kspr_loc_viewport = glGetUniformLocation(m_kspr_shader, "u_viewport");
    m_kspr_loc_sprite   = glGetUniformLocation(m_kspr_shader, "u_sprite");
    m_kspr_loc_palette  = glGetUniformLocation(m_kspr_shader, "u_palette");
    m_kspr_loc_alpha    = glGetUniformLocation(m_kspr_shader, "u_alpha");
    m_kspr_loc_z_ndc    = glGetUniformLocation(m_kspr_shader, "u_z_ndc");
    glUniform1i(m_kspr_loc_sprite,  0);  // GL_TEXTURE0
    glUniform1i(m_kspr_loc_palette, 1);  // GL_TEXTURE1
    glUseProgram(0);

    // Compile the additive glow shader (same vertex shader, glow fragment shader).
    // Uses u_sprite (GL_TEXTURE0), u_viewport, and u_z_ndc only — no palette needed.
    {
        GLuint gv = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,       "kspr_vert.glsl");
        GLuint gf = compile_shader_src(GL_FRAGMENT_SHADER, KSPR_GLOW_FRAGMENT_SHADER, "kspr_glow_frag.glsl");
        if (!gv || !gf)
        {
            if (gv) glDeleteShader(gv);
            if (gf) glDeleteShader(gf);
            ERRORLOG("GLWorldViewRenderer: failed to compile glow shaders");
            return false;
        }
        m_kspr_glow_shader = glCreateProgram();
        glAttachShader(m_kspr_glow_shader, gv);
        glAttachShader(m_kspr_glow_shader, gf);
        glLinkProgram(m_kspr_glow_shader);
        glDeleteShader(gv);
        glDeleteShader(gf);
        GLint glinked = 0;
        glGetProgramiv(m_kspr_glow_shader, GL_LINK_STATUS, &glinked);
        if (!glinked)
        {
            char log[512];
            glGetProgramInfoLog(m_kspr_glow_shader, sizeof(log), nullptr, log);
            ERRORLOG("GLWorldViewRenderer: glow shader link error: %s", log);
            glDeleteProgram(m_kspr_glow_shader);
            m_kspr_glow_shader = 0;
            return false;
        }
        glUseProgram(m_kspr_glow_shader);
        m_kspr_glow_loc_viewport = glGetUniformLocation(m_kspr_glow_shader, "u_viewport");
        m_kspr_glow_loc_sprite   = glGetUniformLocation(m_kspr_glow_shader, "u_sprite");
        m_kspr_glow_loc_z_ndc    = glGetUniformLocation(m_kspr_glow_shader, "u_z_ndc");
        glUniform1i(m_kspr_glow_loc_sprite, 0);  // GL_TEXTURE0
        glUseProgram(0);
    }

    // Reusable 256x256 R8 texture — overwritten per sprite.
    // Zero-initialised so unwritten regions sample as index 0 (transparent)
    // rather than undefined garbage, and to suppress NSight "undefined data" warnings.
    static const uint8_t s_zero_256x256[256 * 256] = {};
    glGenTextures(1, &m_kspr_sprite_tex);
    glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE, s_zero_256x256);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // VAO + VBO: 6 vertices × (vec2 pos + vec2 uv) = 4 floats each
    glGenVertexArrays(1, &m_kspr_vao);
    glGenBuffers(1, &m_kspr_vbo);
    glBindVertexArray(m_kspr_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_kspr_vbo);
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    KFX_GL_LABEL(GL_PROGRAM,      m_kspr_shader,      "WVR/KSprProg");
    KFX_GL_LABEL(GL_PROGRAM,      m_kspr_glow_shader, "WVR/KSprGlowProg");
    KFX_GL_LABEL(GL_TEXTURE,      m_kspr_sprite_tex,  "WVR/KSprSpriteTex");
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_kspr_vao,         "WVR/KSprVAO");
    KFX_GL_LABEL(GL_BUFFER,       m_kspr_vbo,         "WVR/KSprVBO");

    // Attempt to build the sprite decode atlas (GL_TEXTURE_2D_ARRAY).
    // This is an optional optimisation: on first use each unique data pointer
    // is decoded once and cached in a layer of the array, so per-frame decode
    // and per-draw upload are both eliminated for non-remapped sprites.
    // If the driver does not support GL_TEXTURE_2D_ARRAY the atlas is skipped
    // and the existing single-texture fallback path is used unchanged.
    //
    // RenderDoc note: skip entirely when RenderDoc is injected.  RenderDoc's
    // glTexImage3D interceptor for null-data GL_TEXTURE_2D_ARRAY allocations
    // has been observed to raise a blank access-violation (SEH 0xC0000005)
    // caught by RenderDoc's VEH — precisely the "blank exception at heartzoom
    // transition" the user reported.  The atlas is a performance optimisation
    // only; the single-texture fallback path is correct and sufficient.
    do {
        if (platform_is_renderdoc_present()) {
            SYNCLOG("GLWorldViewRenderer: skipping kspr decode atlas (RenderDoc injected)");
            break;
        }

        std::string af_src = KSPR_ARRAY_FRAGMENT_SHADER;

        GLuint av = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,        "kspr_vert.glsl");
        GLuint af = compile_shader_src(GL_FRAGMENT_SHADER, af_src.c_str(), "kspr_array_frag.glsl");
        if (!av || !af) { if (av) glDeleteShader(av); if (af) glDeleteShader(af); break; }

        m_kspr_atlas_shader = glCreateProgram();
        glAttachShader(m_kspr_atlas_shader, av);
        glAttachShader(m_kspr_atlas_shader, af);
        glLinkProgram(m_kspr_atlas_shader);
        glDeleteShader(av);
        glDeleteShader(af);
        GLint linked = 0;
        glGetProgramiv(m_kspr_atlas_shader, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512];
            glGetProgramInfoLog(m_kspr_atlas_shader, sizeof(log), nullptr, log);
            WARNLOG("kspr_array shader link failed: %s", log);
            glDeleteProgram(m_kspr_atlas_shader);
            m_kspr_atlas_shader = 0;
            break;
        }
        glUseProgram(m_kspr_atlas_shader);
        m_kspr_atlas_loc_viewport = glGetUniformLocation(m_kspr_atlas_shader, "u_viewport");
        m_kspr_atlas_loc_sprite   = glGetUniformLocation(m_kspr_atlas_shader, "u_sprite");
        m_kspr_atlas_loc_clut     = glGetUniformLocation(m_kspr_atlas_shader, "u_clut");
        m_kspr_atlas_loc_alpha    = glGetUniformLocation(m_kspr_atlas_shader, "u_alpha");
        m_kspr_atlas_loc_z_ndc    = glGetUniformLocation(m_kspr_atlas_shader, "u_z_ndc");
        m_kspr_atlas_loc_layer    = glGetUniformLocation(m_kspr_atlas_shader, "u_layer");
        m_kspr_atlas_loc_clut_v   = glGetUniformLocation(m_kspr_atlas_shader, "u_clut_v");
        glUniform1i(m_kspr_atlas_loc_sprite, 0);  // GL_TEXTURE0
        glUniform1i(m_kspr_atlas_loc_clut,   1);  // GL_TEXTURE1
        glUseProgram(0);

        // Allocate the texture array.  Clear any pre-existing GL error so the
        // subsequent error check is reliable.
        while (glGetError() != GL_NO_ERROR) {}
        glGenTextures(1, &m_kspr_sprite_array);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);
        glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R8,
                     k_kspr_decode_dim, k_kspr_decode_dim, k_kspr_atlas_layers,
                     0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            WARNLOG("GL_TEXTURE_2D_ARRAY alloc failed (err=%u), sprite atlas disabled", err);
            glDeleteTextures(1, &m_kspr_sprite_array);
            m_kspr_sprite_array = 0;
            glDeleteProgram(m_kspr_atlas_shader);
            m_kspr_atlas_shader = 0;
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            break;
        }
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        KFX_GL_LABEL(GL_PROGRAM, m_kspr_atlas_shader, "WVR/KSprAtlasProg");
        KFX_GL_LABEL(GL_TEXTURE, m_kspr_sprite_array, "WVR/KSprAtlasTex");
        SYNCLOG("GLWorldViewRenderer: keeper-sprite decode atlas ready (%d layers, 256x256 GL_R8)",
                k_kspr_atlas_layers);
    } while (false);

    // Allocate CLUT texture outside the atlas do-while so it is created even if
    // the sprite-array allocation above failed (atlas path requires both, but
    // placing CLUT creation inside meant a break would skip it entirely).
    // CLUT is only useful when the atlas shader compiled successfully.
    if (m_kspr_atlas_shader && !m_kspr_clut_tex)
    {
        glGenTextures(1, &m_kspr_clut_tex);
        glBindTexture(GL_TEXTURE_2D, m_kspr_clut_tex);
        // Zero-initialised: row 0 (identity) is written by ensure_clut_valid() on
        // first frame; until then index 0 = alpha 0 (transparent) is safe.
        // Avoids NSight "undefined data" warnings and prevents garbage colours
        // on the first frame if ensure_clut_valid() hasn't fired yet.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, k_clut_rows,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        {
            // Clear the entire CLUT to zero (transparent black) so unwritten rows
            // never produce garbage.  Only 256×k_clut_rows×4 = 128 KB.
            std::vector<uint8_t> zero_clut(256 * k_clut_rows * 4, 0);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, k_clut_rows,
                            GL_RGBA, GL_UNSIGNED_BYTE, zero_clut.data());
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_kspr_clut_used = 1;
        KFX_GL_LABEL(GL_TEXTURE, m_kspr_clut_tex, "WVR/KSprCLUT");
    }

    // ── Depth-fail outline shaders ────────────────────────────────────────────
    // Two variants: single-texture (fallback path) and array-atlas (cached path).
    // Compiled opportunistically — missing shaders just disable the outline.
    {
        std::string of_src  = KSPR_OUTLINE_FRAGMENT_SHADER;
        std::string oaf_src = KSPR_ARRAY_OUTLINE_FRAGMENT_SHADER;

        if (!of_src.empty())
        {
            GLuint ov = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,           "kspr_vert.glsl");
            GLuint of = compile_shader_src(GL_FRAGMENT_SHADER, of_src.c_str(), "kspr_outline_frag.glsl");
            if (ov && of)
            {
                m_kspr_outline_shader = glCreateProgram();
                glAttachShader(m_kspr_outline_shader, ov);
                glAttachShader(m_kspr_outline_shader, of);
                glLinkProgram(m_kspr_outline_shader);
                glDeleteShader(ov); glDeleteShader(of);
                GLint ol = 0;
                glGetProgramiv(m_kspr_outline_shader, GL_LINK_STATUS, &ol);
                if (ol)
                {
                    glUseProgram(m_kspr_outline_shader);
                    m_kspr_outline_loc_viewport = glGetUniformLocation(m_kspr_outline_shader, "u_viewport");
                    m_kspr_outline_loc_sprite   = glGetUniformLocation(m_kspr_outline_shader, "u_sprite");
                    m_kspr_outline_loc_z_ndc    = glGetUniformLocation(m_kspr_outline_shader, "u_z_ndc");
                    m_kspr_outline_loc_color    = glGetUniformLocation(m_kspr_outline_shader, "u_outline_color");
                    glUniform1i(m_kspr_outline_loc_sprite, 0); // GL_TEXTURE0
                    glUseProgram(0);
                    KFX_GL_LABEL(GL_PROGRAM, m_kspr_outline_shader, "WVR/KSprOutlineProg");
                }
                else
                {
                    char log[512];
                    glGetProgramInfoLog(m_kspr_outline_shader, sizeof(log), nullptr, log);
                    WARNLOG("GLWorldViewRenderer: kspr_outline shader link failed: %s", log);
                    glDeleteProgram(m_kspr_outline_shader);
                    m_kspr_outline_shader = 0;
                }
            }
            else { if (ov) glDeleteShader(ov); if (of) glDeleteShader(of); }
        }

        if (!oaf_src.empty() && m_kspr_atlas_shader)
        {
            GLuint oav = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,                 "kspr_vert.glsl");
            GLuint oaf = compile_shader_src(GL_FRAGMENT_SHADER, oaf_src.c_str(), "kspr_array_outline_frag.glsl");
            if (oav && oaf)
            {
                m_kspr_atlas_outline_shader = glCreateProgram();
                glAttachShader(m_kspr_atlas_outline_shader, oav);
                glAttachShader(m_kspr_atlas_outline_shader, oaf);
                glLinkProgram(m_kspr_atlas_outline_shader);
                glDeleteShader(oav); glDeleteShader(oaf);
                GLint oal = 0;
                glGetProgramiv(m_kspr_atlas_outline_shader, GL_LINK_STATUS, &oal);
                if (oal)
                {
                    glUseProgram(m_kspr_atlas_outline_shader);
                    m_kspr_atlas_outline_loc_viewport = glGetUniformLocation(m_kspr_atlas_outline_shader, "u_viewport");
                    m_kspr_atlas_outline_loc_sprite   = glGetUniformLocation(m_kspr_atlas_outline_shader, "u_sprite");
                    m_kspr_atlas_outline_loc_z_ndc    = glGetUniformLocation(m_kspr_atlas_outline_shader, "u_z_ndc");
                    m_kspr_atlas_outline_loc_color    = glGetUniformLocation(m_kspr_atlas_outline_shader, "u_outline_color");
                    m_kspr_atlas_outline_loc_layer    = glGetUniformLocation(m_kspr_atlas_outline_shader, "u_layer");
                    glUniform1i(m_kspr_atlas_outline_loc_sprite, 0); // GL_TEXTURE0
                    glUseProgram(0);
                    KFX_GL_LABEL(GL_PROGRAM, m_kspr_atlas_outline_shader, "WVR/KSprAtlasOutlineProg");
                }
                else
                {
                    char log[512];
                    glGetProgramInfoLog(m_kspr_atlas_outline_shader, sizeof(log), nullptr, log);
                    WARNLOG("GLWorldViewRenderer: kspr_array_outline shader link failed: %s", log);
                    glDeleteProgram(m_kspr_atlas_outline_shader);
                    m_kspr_atlas_outline_shader = 0;
                }
            }
            else { if (oav) glDeleteShader(oav); if (oaf) glDeleteShader(oaf); }
        }
    }

    // ── Atlas glow shader (additive sprites via atlas) ────────────────────────
    if (m_kspr_atlas_shader)
    {
        GLuint gav = compile_shader_src(GL_VERTEX_SHADER,   KSPR_VERTEX_SHADER,              "kspr_vert.glsl");
        GLuint gaf = compile_shader_src(GL_FRAGMENT_SHADER, KSPR_ARRAY_GLOW_FRAGMENT_SHADER, "kspr_array_glow_frag.glsl");
        if (gav && gaf)
        {
            m_kspr_atlas_glow_shader = glCreateProgram();
            glAttachShader(m_kspr_atlas_glow_shader, gav);
            glAttachShader(m_kspr_atlas_glow_shader, gaf);
            glLinkProgram(m_kspr_atlas_glow_shader);
            glDeleteShader(gav); glDeleteShader(gaf);
            GLint gal = 0;
            glGetProgramiv(m_kspr_atlas_glow_shader, GL_LINK_STATUS, &gal);
            if (gal)
            {
                glUseProgram(m_kspr_atlas_glow_shader);
                m_kspr_atlas_glow_loc_viewport = glGetUniformLocation(m_kspr_atlas_glow_shader, "u_viewport");
                m_kspr_atlas_glow_loc_sprite   = glGetUniformLocation(m_kspr_atlas_glow_shader, "u_sprite");
                m_kspr_atlas_glow_loc_z_ndc    = glGetUniformLocation(m_kspr_atlas_glow_shader, "u_z_ndc");
                m_kspr_atlas_glow_loc_layer    = glGetUniformLocation(m_kspr_atlas_glow_shader, "u_layer");
                glUniform1i(m_kspr_atlas_glow_loc_sprite, 0);  // GL_TEXTURE0
                glUseProgram(0);
                KFX_GL_LABEL(GL_PROGRAM, m_kspr_atlas_glow_shader, "WVR/KSprAtlasGlowProg");
            }
            else
            {
                char log[512];
                glGetProgramInfoLog(m_kspr_atlas_glow_shader, sizeof(log), nullptr, log);
                WARNLOG("GLWorldViewRenderer: kspr_array_glow shader link failed: %s", log);
                glDeleteProgram(m_kspr_atlas_glow_shader);
                m_kspr_atlas_glow_shader = 0;
            }
        }
        else { if (gav) glDeleteShader(gav); if (gaf) glDeleteShader(gaf); }
    }

    SYNCLOG("GLWorldViewRenderer: keeper-sprite shader initialised");
    return true;
}

bool GLWorldViewRenderer::init_flatpoly_shader()
{
    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   FLATPOLY_VERTEX_SHADER,   "flatpoly_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, FLATPOLY_FRAGMENT_SHADER, "flatpoly_frag.glsl");
    if (!sv || !sf)
    {
        if (sv) glDeleteShader(sv);
        if (sf) glDeleteShader(sf);
        return false;
    }
    m_flatpoly_shader = glCreateProgram();
    glAttachShader(m_flatpoly_shader, sv);
    glAttachShader(m_flatpoly_shader, sf);
    glLinkProgram(m_flatpoly_shader);
    glDeleteShader(sv);
    glDeleteShader(sf);

    GLint linked = 0;
    glGetProgramiv(m_flatpoly_shader, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(m_flatpoly_shader, sizeof(log), nullptr, log);
        ERRORLOG("GLWorldViewRenderer: flat-poly shader link error: %s", log);
        glDeleteProgram(m_flatpoly_shader);
        m_flatpoly_shader = 0;
        return false;
    }

    glUseProgram(m_flatpoly_shader);
    m_flatpoly_loc_viewport = glGetUniformLocation(m_flatpoly_shader, "u_viewport");
    glUseProgram(0);

    KFX_GL_LABEL(GL_PROGRAM, m_flatpoly_shader, "WVR/FlatPolyProg");

    // VAO + dynamic VBO: 6 floats per vertex (x, y, z, r, g, b)
    glGenVertexArrays(1, &m_flatpoly_vao);
    glGenBuffers(1, &m_flatpoly_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_flatpoly_vao, "WVR/FlatPolyVAO");
    KFX_GL_LABEL(GL_BUFFER,       m_flatpoly_vbo, "WVR/FlatPolyVBO");
    glBindVertexArray(m_flatpoly_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_flatpoly_vbo);
    // layout(location=0) vec3 a_pos  (x, y = screen px; z = NDC)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // layout(location=1) vec3 a_color (linear RGB)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    return true;
}

/******************************************************************************/

void GLWorldViewRenderer::BeginCursorCapture()
{
    ASSERT_GAME_THREAD();
    // No clear here: FlipBuffers() empties m_cursor_kspr_ir via std::move, so
    // the buffer is already clean at frame start.  Multiple Begin/End pairs
    // within one frame safely accumulate into the same buffer.
    m_cursor_capture = true;
}

void GLWorldViewRenderer::EndCursorCapture()
{
    ASSERT_GAME_THREAD();
    m_cursor_capture = false;
}

void GLWorldViewRenderer::DrawCursorKeeperSprites()
{
    ASSERT_RENDER_THREAD();
    if (m_rt_cursor_kspr_ir.empty()) return;

    const int saved_draw_w   = m_draw_screen_w;
    const int saved_draw_h   = m_draw_screen_h;
    const float saved_z      = m_current_sprite_z;

    m_draw_screen_w    = m_full_screen_w;
    m_draw_screen_h    = m_full_screen_h;
    m_current_sprite_z = -1.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, m_full_screen_w, m_full_screen_h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for (const auto& cmd : m_rt_cursor_kspr_ir)
    {
        render_keepersprite_gpu(cmd.dst_x, cmd.dst_y, cmd.dst_w, cmd.dst_h,
                                cmd.data, cmd.src_w, cmd.src_h,
                                cmd.draw_flags, cmd.remap,
                                cmd.z_ndc, cmd.owner, cmd.wants_outline);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    m_draw_screen_w    = saved_draw_w;
    m_draw_screen_h    = saved_draw_h;
    m_current_sprite_z = saved_z;
}

/******************************************************************************/

int GLWorldViewRenderer::SubmitKeeperSprite(
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    const unsigned char* data, int src_w, int src_h,
    unsigned int draw_flags, const unsigned char* remap)
{

    if (src_w <= 0 || src_h <= 0 || src_w > k_kspr_decode_dim || src_h > k_kspr_decode_dim) {
        static int s_dim = 0;
        if (s_dim++ < 20)
            WARNLOG("SubmitKeeperSprite: invalid sprite dimensions %dx%d (max %d) — dropped",
                    src_w, src_h, k_kspr_decode_dim);
        return 1;
    }
    if (dst_w <= 0 || dst_h <= 0) return 1;

    // Cursor capture mode: game thread is pre-computing cursor sprites.
    // Store in cursor shadow buffer; render thread draws them via DrawCursorKeeperSprites().
    if (m_cursor_capture)
    {
        IRWorldKeeperSpriteCmd cmd;
        cmd.dst_x         = dst_x;
        cmd.dst_y         = dst_y;
        cmd.dst_w         = dst_w;
        cmd.dst_h         = dst_h;
        cmd.src_w         = src_w;
        cmd.src_h         = src_h;
        cmd.draw_flags    = draw_flags;
        cmd.data          = data;
        cmd.remap         = remap;
        cmd.z_ndc         = -1.0f;
        cmd.owner         = (int8_t)WorldViewRenderer_GetCurrentSpriteOwner();
        cmd.wants_outline = (int8_t)WorldViewRenderer_GetCurrentSpriteWantsOutline();
        m_cursor_kspr_ir.push_back(cmd);
        return 1;
    }

    // Choose the write target: PiP capture goes to m_pip_kspr_ir, normal to m_kspr_ir.
    auto& kspr_buf = m_pip_capture ? m_pip_kspr_ir : m_kspr_ir;

    IRWorldKeeperSpriteCmd cmd;
    cmd.dst_x         = dst_x;
    cmd.dst_y         = dst_y;
    cmd.dst_w         = dst_w;
    cmd.dst_h         = dst_h;
    cmd.src_w         = src_w;
    cmd.src_h         = src_h;
    cmd.draw_flags    = draw_flags;
    cmd.data          = data;
    cmd.remap         = remap;
    cmd.z_ndc         = m_current_sprite_z;
    cmd.owner         = (int8_t)WorldViewRenderer_GetCurrentSpriteOwner();
    cmd.wants_outline = (int8_t)WorldViewRenderer_GetCurrentSpriteWantsOutline();
    kspr_buf.push_back(cmd);
    return 1;
}

/******************************************************************************/

int GLWorldViewRenderer::render_keepersprite_gpu(
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    const unsigned char* data, int src_w, int src_h,
    unsigned int draw_flags, const unsigned char* remap,
    float z_ndc, int sprite_owner, int sprite_wants_outline)
{
    // GL resources must be ready before any sprite is submitted.  If they are
    // not, this is a hard initialisation failure — never fall back to the CPU
    // software rasteriser from here; that would produce mixed-path frames.
    if (!m_kspr_shader || !m_kspr_sprite_tex || !m_palette_tex) {
        static int s_miss = 0;
        if (s_miss++ < 5)
            ERRORLOG("render_keepersprite_gpu: GL resources not ready (shader=%u, sprite_tex=%u, palette_tex=%u) — sprite dropped, NOT sent to software",
                     m_kspr_shader, m_kspr_sprite_tex, m_palette_tex);
        return 1; // Claim the sprite: do NOT fall back to CPU rasteriser.
    }
    if (src_w <= 0 || src_h <= 0 || src_w > k_kspr_decode_dim || src_h > k_kspr_decode_dim) {
        static int s_dim = 0;
        if (s_dim++ < 20)
            WARNLOG("render_keepersprite_gpu: invalid sprite dimensions %dx%d (max %d) — dropped",
                    src_w, src_h, k_kspr_decode_dim);
        return 1;
    }
    if (dst_w <= 0 || dst_h <= 0) return 1;

    // Upload palette on first sprite of this frame
    // Palette is shared from RendererOpenGL — already uploaded every frame.
    // No per-subsystem palette upload needed.

    // --- Sprite decode atlas ---
    // The atlas caches pre-decoded sprites for the lifetime of a level so
    // decode_keeper_rle + glTexSubImage2D run at most once per unique data
    // pointer. Subsequent draws of the same sprite just bind the cached
    // layer with no CPU decode and no GPU upload.
    const bool additive = (draw_flags & Lb_SPRITE_ALPHA_ADDITIVE) != 0;
    const bool use_remap = remap && (draw_flags & Lb_TEXT_UNDERLNSHADOW) && !additive;
    const bool atlas_eligible = m_kspr_sprite_array && m_kspr_atlas_shader && m_kspr_clut_tex;
    int atlas_layer = -1;
    if (atlas_eligible)
    {
        auto it = m_kspr_atlas_map.find(data);
        if (it != m_kspr_atlas_map.end())
        {
            atlas_layer = it->second.layer;
            // src_w for this cached entry may differ from current clip height;
            // UV will use the passed-in src_h so only the visible rows are sampled.
            m_kspr_atlas_hits++;
        }
        else if (m_kspr_atlas_used < k_kspr_atlas_layers)
        {
            KFX_ZONE_COLOR("WVR::KSprAtlas::Decode+Upload", KFX_COLOR_RENDER_CPU);
            // Clear the full scratch buffer before decoding so that rows beyond
            // src_h (uploaded at full k_kspr_decode_dim height) are transparent
            // (palette index 0).  Without this, a previously decoded taller sprite
            // leaves stale pixel data in the atlas layer beyond src_h rows, which
            // becomes visible if the same data pointer is later drawn with a
            // larger src_h (e.g. after water_source_cutoff shrinks to zero).
            memset(s_kspr_decode_buf, 0, sizeof(s_kspr_decode_buf));
            decode_keeper_rle(s_kspr_decode_buf, data, src_w, src_h);
            atlas_layer = m_kspr_atlas_used++;
            if (m_kspr_atlas_used > m_kspr_atlas_peak)
            {
                m_kspr_atlas_peak = m_kspr_atlas_used;
                SYNCLOG("GLWorldViewRenderer: sprite atlas peak = %d / %d layers (%.1f MB GL_R8)",
                        m_kspr_atlas_peak, k_kspr_atlas_layers,
                        m_kspr_atlas_peak * k_kspr_decode_dim * k_kspr_decode_dim / (1024.0f * 1024.0f));
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, k_kspr_decode_dim);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                            0, 0, atlas_layer,
                            src_w, k_kspr_decode_dim, 1,
                            GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            m_kspr_atlas_map[data] = {atlas_layer, src_w};
            m_kspr_atlas_misses++;
        }
    }

    // ── CLUT row selection ────────────────────────────────────────────────────
    // Row 0 = identity (non-remapped sprites).
    // Rows 1..k_clut_rows-1 = per-remap CLUTs built lazily from current palette.
    float clut_v = 0.5f / (float)k_clut_rows;  // row 0 centre
    if (atlas_layer >= 0 && use_remap && m_kspr_clut_tex)
    {
        auto clut_it = m_kspr_clut_map.find(remap);
        if (clut_it != m_kspr_clut_map.end())
        {
            clut_v = (float(clut_it->second) + 0.5f) / (float)k_clut_rows;
        }
        else if (m_kspr_clut_used < k_clut_rows)
        {
            int row_idx = m_kspr_clut_used++;
            uint8_t row_data[256 * 4];
            for (int ci = 0; ci < 256; ci++) {
                int ri = remap[ci];
                row_data[ci*4+0] = (uint8_t)(m_rt_palette[ri*3+0] << 2);
                row_data[ci*4+1] = (uint8_t)(m_rt_palette[ri*3+1] << 2);
                row_data[ci*4+2] = (uint8_t)(m_rt_palette[ri*3+2] << 2);
                row_data[ci*4+3] = (ci == 0) ? 0 : 255;
            }
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_kspr_clut_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, row_idx, 256, 1,
                            GL_RGBA, GL_UNSIGNED_BYTE, row_data);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);  // restore active unit before main draw setup
            m_kspr_clut_map[remap] = row_idx;
            clut_v = (float(row_idx) + 0.5f) / (float)k_clut_rows;
        }
        // If CLUT is full, clut_v stays at row 0 (identity fallback — wrong colour
        // but no crash). Increase k_clut_rows if this ever triggers.
    }

    float u1 = (float)src_w / (float)k_kspr_decode_dim;
    float v1 = (float)src_h / (float)k_kspr_decode_dim;
    float ul = 0.0f, ur = u1;
    if (draw_flags & Lb_SPRITE_FLIP_HORIZ) { ul = u1; ur = 0.0f; }

    float vx0 = (float)dst_x,          vy0 = (float)dst_y;
    float vx1 = (float)(dst_x + dst_w), vy1 = (float)(dst_y + dst_h);

    float sv[6][4] = {
        { vx0, vy0, ul,  0.0f },   // TL
        { vx1, vy0, ur,  0.0f },   // TR
        { vx1, vy1, ur,  v1   },   // BR
        { vx0, vy0, ul,  0.0f },   // TL
        { vx1, vy1, ur,  v1   },   // BR
        { vx0, vy1, ul,  v1   },   // BL
    };
    glBindBuffer(GL_ARRAY_BUFFER, m_kspr_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(sv), nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);

    float alpha = 1.0f;
    if      (draw_flags & Lb_SPRITE_TRANSPAR4) alpha = g_renderer_settings.transpar4_alpha;
    else if (draw_flags & Lb_SPRITE_TRANSPAR8) alpha = g_renderer_settings.transpar8_alpha;

    glBindVertexArray(m_kspr_vao);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    // ── Depth-fail outline pass ──────────────────────────────────────────────
    // Draws a flat owner-colour silhouette only where the sprite is BEHIND
    // geometry (GL_GREATER depth test).  The normal draw below then paints
    // over the visible portion with GL_LEQUAL, leaving the outline only
    // peeking out from behind walls/columns.
    // Additive glow sprites are excluded (no meaningful silhouette).
    // The class-mask in RendererSettings controls which entity types are outlined.
    const bool wants_outline = sprite_wants_outline != 0;
    if (g_renderer_settings.creature_outline_enable && !additive && wants_outline)
    {
        // Resolve owner → player colour index → linear RGB from the palette.
        float oc_r = 0.9f, oc_g = 0.9f, oc_b = 0.9f;
        int owner = sprite_owner;
        if (owner >= 0)
        {
            unsigned char color_idx = get_player_color_idx((PlayerNumber)owner);
            if (color_idx < 9)
            {
                uint8_t pal_idx = player_room_colours[color_idx];
                oc_r = (float)((int)m_rt_palette[pal_idx * 3 + 0] << 2) / 255.0f;
                oc_g = (float)((int)m_rt_palette[pal_idx * 3 + 1] << 2) / 255.0f;
                oc_b = (float)((int)m_rt_palette[pal_idx * 3 + 2] << 2) / 255.0f;
            }
        }
        const float oc_a = g_renderer_settings.creature_outline_alpha;

        glDepthFunc(GL_GREATER);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Push the outline slightly farther from the camera so it only appears
        // when the sprite is meaningfully behind geometry, not at tile edges
        // where depth values are nearly equal (avoids stray corner pixels).
        const float outline_z = z_ndc + 0.002f;

        if (atlas_layer >= 0 && m_kspr_atlas_outline_shader)
        {
            // Atlas path: texture already cached in m_kspr_sprite_array.
            glUseProgram(m_kspr_atlas_outline_shader);
            glUniform2f(m_kspr_atlas_outline_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
            glUniform1f(m_kspr_atlas_outline_loc_z_ndc,    outline_z);
            glUniform1f(m_kspr_atlas_outline_loc_layer,    (float)atlas_layer);
            glUniform4f(m_kspr_atlas_outline_loc_color,    oc_r, oc_g, oc_b, oc_a);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        else if (atlas_layer < 0 && m_kspr_outline_shader)
        {
            // Fallback path: decode + upload now so the outline has pixel data.
            // The normal draw below will re-use the already-uploaded texture.
            decode_keeper_rle(s_kspr_decode_buf, data, src_w, src_h);
            if (use_remap)
            {
                for (int oy = 0; oy < src_h; ++oy) {
                    uint8_t* orow = s_kspr_decode_buf + oy * k_kspr_decode_dim;
                    for (int ox = 0; ox < src_w; ++ox)
                        if (orow[ox] != 0) orow[ox] = remap[orow[ox]];
                }
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, k_kspr_decode_dim);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h,
                            GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

            glUseProgram(m_kspr_outline_shader);
            glUniform2f(m_kspr_outline_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
            glUniform1f(m_kspr_outline_loc_z_ndc,    outline_z);
            glUniform4f(m_kspr_outline_loc_color,    oc_r, oc_g, oc_b, oc_a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        glDepthFunc(GL_LEQUAL); // restore for normal draw below
    }

    if (atlas_layer >= 0) {
        if (additive && m_kspr_atlas_glow_shader) {
            // Additive glow via atlas: no CLUT needed, glow math in shader.
            glUseProgram(m_kspr_atlas_glow_shader);
            glUniform2f(m_kspr_atlas_glow_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
            glUniform1f(m_kspr_atlas_glow_loc_z_ndc, z_ndc);
            glUniform1f(m_kspr_atlas_glow_loc_layer, (float)atlas_layer);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);
            glBlendFunc(GL_ONE, GL_ONE);
        } else {
            // Normal or remapped sprite via atlas + CLUT.
            glUseProgram(m_kspr_atlas_shader);
            glUniform2f(m_kspr_atlas_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
            glUniform1f(m_kspr_atlas_loc_alpha, alpha);
            glUniform1f(m_kspr_atlas_loc_z_ndc, z_ndc);
            glUniform1f(m_kspr_atlas_loc_layer, (float)atlas_layer);
            glUniform1f(m_kspr_atlas_loc_clut_v, clut_v);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_kspr_clut_tex);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_kspr_sprite_array);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // Always restore standard blend so the next sprite (or tile pass) is
        // not accidentally drawn with additive blend left over from a glow sprite.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glUseProgram(0);
        return 1;
    }
    // Used for remapped sprites, additive glow sprites, and when atlas is full.
    // If the outline block above already decoded + uploaded the sprite, skip
    // the decode/upload here to avoid redundant work.
    const bool outline_uploaded = g_renderer_settings.creature_outline_enable
                                   && !additive
                                   && wants_outline
                                   && atlas_layer < 0
                                   && m_kspr_outline_shader;
    if (!outline_uploaded)
    {
        // Decode RLE into the scratch buffer (stride = k_kspr_decode_dim)
        decode_keeper_rle(s_kspr_decode_buf, data, src_w, src_h);

        // Apply colour remap (white/red highlight, shade fade, tint).
        if (use_remap)
        {
            for (int y = 0; y < src_h; ++y) {
                uint8_t* row = s_kspr_decode_buf + y * k_kspr_decode_dim;
                for (int x = 0; x < src_w; ++x)
                    if (row[x] != 0) row[x] = remap[row[x]];
            }
        }

        // Upload decoded sprite to scratch texture (stride = k_kspr_decode_dim)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, k_kspr_decode_dim);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h,
                        GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }

    if (additive && m_kspr_glow_shader)
    {
        // Pure additive blend: adds glow RGB delta to framebuffer contents.
        glUseProgram(m_kspr_glow_shader);
        glUniform2f(m_kspr_glow_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
        glUniform1f(m_kspr_glow_loc_z_ndc,    z_ndc);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);

        glBlendFunc(GL_ONE, GL_ONE);
    }
    else
    {
        glUseProgram(m_kspr_shader);
        glUniform2f(m_kspr_loc_viewport, (float)m_draw_screen_w, (float)m_draw_screen_h);
        glUniform1f(m_kspr_loc_alpha,    alpha);
        glUniform1f(m_kspr_loc_z_ndc,    z_ndc);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_palette_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Restore standard blend — additive glow path sets GL_ONE,GL_ONE.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glUseProgram(0);

    return 1;
}

/******************************************************************************/

void GLWorldViewRenderer::DrawKeeperSpriteGL(const IRWorldKeeperSpriteCmd& cmd)
{
    ASSERT_RENDER_THREAD();
    render_keepersprite_gpu(cmd.dst_x, cmd.dst_y, cmd.dst_w, cmd.dst_h,
                            cmd.data, cmd.src_w, cmd.src_h, cmd.draw_flags, cmd.remap,
                            cmd.z_ndc, (int)cmd.owner, (int)cmd.wants_outline);
}

/******************************************************************************/

// Game-thread: sets m_current_sprite_z for SubmitKeeperSprite to capture into IR.
void GLWorldViewRenderer::setup_world_sprite_processing(int32_t bucket_num)
{
    if (!m_initialized) {
        ERRORLOG("setup_world_sprite_processing: renderer not initialised — sprite processing skipped");
        return;
    }
    //  biased half a bucket closer to the camera, so sprites always pass the depth test against same-bucket ground polygons
    // (avoids z-fighting between coplanar sprites and tiles).
    const float sprite_z = 2.0f * ((float)bucket_num - 0.5f) / (float)(BUCKETS_COUNT - 1) - 1.0f;
    // Stored here so that keeper-sprite (KSprite) render passes and UIRenderer
    // (JontySprites) both receive the same biased z via UIRenderer_BeginWorldDepth().
    m_current_sprite_z = sprite_z;
}

/******************************************************************************/

void GLWorldViewRenderer::BeginWorldPass(unsigned char* framebuf, int pitch,
                                          int w, int h, int vp_x, int vp_y)
{
    KFX_ZONE("WVR::BeginWorldPass");
    ASSERT_GAME_THREAD();

    // Since the render thread no longer accesses poly_pool (bucket bucket-walks moved
    // to the game thread in Phase 1), no fence wait is needed here.

    m_screen_w        = w;
    m_screen_h        = h;
    m_vp_x            = vp_x;
    m_vp_y            = vp_y;
    m_framebuf        = framebuf;
    m_pitch           = pitch;
    m_current_bucket    = 0;
    m_world_pass_active  = true;
    m_kspr_atlas_hits    = 0;
    m_kspr_atlas_misses  = 0;

    // Flush any tile batch from the *previous* sub-pass before starting the
    // new one — but do NOT clear the draw list.  Multiple sub-passes per frame
    // (isometric view + front view, or creature possession + normal view) must
    // accumulate into a single draw list so GPUFlushNow sees all geometry.
    // We DO need to reset the vertex write cursor so the new pass starts fresh
    // with its own section of the staging vertex buffer.
    gpu_flush();  // close any open tile batch from prior pass (uses pip buffers if m_pip_capture)
    if (m_pip_capture)
        m_pip_cmd_vert_start = m_pip_vert_count;  // new PiP pass appends after prior pip verts
    else
        m_cmd_vert_start = m_vert_count;          // new pass appends after prior pass verts

    // Lazy initialise GL resources on first use (GL context must be current)
    if (!m_initialized)
        init_gl_resources();

    // Lazy-retry atlas upload once block_mem is populated by the game data loader.
    if (m_atlas && !m_atlas->IsInitialized() && block_mem != nullptr)
    {
        m_atlas->Init();
        if (!m_atlas->IsInitialized())
            WARNLOG("GLWorldViewRenderer: tile atlas not yet ready on BeginWorldPass");
    }

    if (m_initialized)
    {
        // GPU path: set the vec globals that bucket-list filling code reads
        // (setup_rotate_stuff, fill_in_points_*, etc.) without invoking the
        // full software fallback which would also zero WScreen and set
        // vec_screen / poly_screen for the CPU rasteriser — both unnecessary.
        if (w > 0)  vec_window_width  = (long)w;
        if (h > 0)  vec_window_height = (long)h;
    }
    else
    {
    }

}

/******************************************************************************/

bool GLWorldViewRenderer::append_triangle(int tile_id,
                                           const struct PolyPoint* p0,
                                           const struct PolyPoint* p1,
                                           const struct PolyPoint* p2,
                                           int32_t cam_z0, int32_t cam_z1, int32_t cam_z2)
{
    const int variation  = tile_id / TEXTURE_BLOCKS_COUNT;
    const int tile_local = tile_id % TEXTURE_BLOCKS_COUNT;

    int& vcnt = m_pip_capture ? m_pip_vert_count : m_vert_count;
    if (vcnt + 3 > k_max_verts)
    {
        gpu_flush();
        if (vcnt + 3 > k_max_verts)
            return false; // buffer full; drop gracefully rather than write OOB
    }

    std::vector<WorldVertex>* verts_vec = nullptr;
    if (m_pip_capture)
    {
        verts_vec = &m_pip_verts;
    }
    else
    {
        if (!m_world_write_cmds)
            return false;
        verts_vec = &m_world_write_cmds->tile_verts;
    }
    verts_vec->resize((size_t)(vcnt + 3));

    // Look up the normalised UV rectangle for this tile in the atlas.
    float u0f, v0f, u1f, v1f;
    TileAtlasPacker::GetTileUV(tile_local, &u0f, &v0f, &u1f, &v1f);

    // Derive NDC depth from the painter's-algorithm bucket index:
    //   high bi (far away) → z near +1.0; low bi (close) → z near -1.0.
    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;
    const float layer = (float)variation;

    WorldVertex* const verts = verts_vec->data();
    const struct PolyPoint* pts[3] = { p0, p1, p2 };
    const int32_t cam_z[3] = { cam_z0, cam_z1, cam_z2 };
    for (int i = 0; i < 3; i++)
    {
        WorldVertex* wv = &verts[vcnt + i];
        // X/Y are integer screen pixels (not 16:16 fixed-point).
        wv->x = (float)(pts[i]->X) / (float)m_screen_w * 2.0f - 1.0f;
        wv->y = 1.0f - (float)(pts[i]->Y) / (float)m_screen_h * 2.0f;
        wv->z = z_ndc;
        // U/V are 16:16; integer part (>>16) is texel 0..31 within the 32-px tile.
        wv->u = u0f + ((float)(pts[i]->U >> 16) / 32.0f) * (u1f - u0f);
        wv->v = v0f + ((float)(pts[i]->V >> 16) / 32.0f) * (v1f - v0f);
        // S = shade_intensity<<8; (S>>16) gives shade level 0..62.
        // The software fade table reaches 100% brightness at row 32; rows 33-62
        // are clamped over-bright.  Dividing by 32 matches: values >1 clamp in GLSL.
        wv->shade = (float)(pts[i]->S >> 16) / 32.0f;
        wv->stl_x = 0.0f;  // Phase 3: fill from tile map position
        wv->stl_y = 0.0f;
        // Camera-space Z for perspective-correct interpolation.
        // 0 or negative means unknown — default to 1.0 (affine, no correction).
        wv->camera_z = (cam_z[i] > 0) ? (float)cam_z[i] : 1.0f;
        wv->atlas_layer = layer;
    }
    vcnt += 3;
    return true;
}

bool GLWorldViewRenderer::append_triangle_compact(
    int sx0, int sy0, int u0, int v0, int shade0,
    int sx1, int sy1, int u1, int v1, int shade1,
    int sx2, int sy2, int u2, int v2, int shade2)
{
    // Compact UV (0..255) addresses the original 8-tiles-wide source layout:
    //   column in 8-wide layout = u8 / 32,  within-tile x = u8 % 32
    //   row in source layout    = v8 / 32,  within-tile y = v8 % 32
    //   tile_id = tile_row * block_count_per_row + tile_col
    // The atlas is repacked 64 tiles wide (2048 px), so we must remap via
    // GetTileUV just as append_triangle does for full PolyPoint polygons.
    // Compact triangles always reference tiles with variation 0 (tile_id < TEXTURE_BLOCKS_COUNT);
    // atlas_layer is set to 0.0f in the COMPACT_UV_TO_WORLDVERTEX macro.

    int& vcnt = m_pip_capture ? m_pip_vert_count : m_vert_count;
    if (vcnt + 3 > k_max_verts)
    {
        gpu_flush();
        if (vcnt + 3 > k_max_verts)
            return false; // buffer full; drop gracefully rather than write OOB
    }

    std::vector<WorldVertex>* verts_vec = nullptr;
    if (m_pip_capture)
    {
        verts_vec = &m_pip_verts;
    }
    else
    {
        if (!m_world_write_cmds)
            return false;
        verts_vec = &m_world_write_cmds->tile_verts;
    }
    verts_vec->resize((size_t)(vcnt + 3));

    // Helper: derive atlas UV from a compact (u8, v8) coordinate pair.
    // The compact coords index the 8-column source layout; map to atlas via GetTileUV.
    auto compact_to_atlas = [](int u8, int v8, float& out_u, float& out_v)
    {
        const int tile_col = (u8 >> 5) & 7;   // u8 / 32 — column in 8-wide source (0-7)
        const int within_x =  u8 & 31;         // u8 % 32 — pixel within tile horizontally
        const int tile_row =  v8 >> 5;          // v8 / 32 — tile row in source
        const int within_y =  v8 & 31;          // v8 % 32 — pixel within tile vertically
        const int tile_id  = tile_row * (int)block_count_per_row + tile_col;
        float u0f, v0f, u1f, v1f;
        TileAtlasPacker::GetTileUV(tile_id, &u0f, &v0f, &u1f, &v1f);
        // Remap within-tile pixel (0..31) to the atlas UV range for this tile,
        // matching the same / 32.0f convention used in append_triangle.
        out_u = u0f + ((float)within_x / 32.0f) * (u1f - u0f);
        out_v = v0f + ((float)within_y / 32.0f) * (v1f - v0f);
    };

    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;

    WorldVertex* const verts = verts_vec->data();
    WorldVertex* v = &verts[vcnt];

    // Build screen-space XY and shade via the existing macro, then overwrite
    // the UV fields with the correctly remapped atlas coordinates.
    COMPACT_UV_TO_WORLDVERTEX(&v[0], sx0, sy0, u0, v0, shade0, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[1], sx1, sy1, u1, v1, shade1, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[2], sx2, sy2, u2, v2, shade2, m_screen_w, m_screen_h);
    compact_to_atlas(u0, v0, v[0].u, v[0].v);
    compact_to_atlas(u1, v1, v[1].u, v[1].v);
    compact_to_atlas(u2, v2, v[2].u, v[2].v);
    v[0].z = z_ndc; v[1].z = z_ndc; v[2].z = z_ndc;
    vcnt += 3;
    return true;
}

void GLWorldViewRenderer::gpu_flush()
{
    int& vcnt    = m_pip_capture ? m_pip_vert_count     : m_vert_count;
    int& vstart  = m_pip_capture ? m_pip_cmd_vert_start : m_cmd_vert_start;
    std::vector<DrawCmd>& dcmds = m_pip_capture ? m_pip_draw_cmds : m_draw_cmds;

    if (!m_initialized || vcnt <= vstart)
        return;

    // Record the current tile batch as a deferred draw command.
    // No GL calls are issued here — everything is replayed in GPURenderNow()
    // AFTER RendererOpenGL::EndFrame() calls glClear().
    DrawCmd cmd;
    cmd.type       = DrawCmd::CMD_TILES;
    cmd.vert_start = vstart;
    cmd.vert_count = vcnt - vstart;
    dcmds.push_back(cmd);
    vstart = vcnt;
}

void GLWorldViewRenderer::FlipBuffers()
{
    ASSERT_GAME_THREAD();
    // Move game-thread buffers into the render-thread shadow copies.
    // After the move, game-side containers are empty and ready for the next frame.
    m_rt_draw_cmds      = std::move(m_draw_cmds);
    m_rt_shadow_cmds    = std::move(m_shadow_cmds);
    m_rt_kspr_ir        = std::move(m_kspr_ir);
    m_rt_cursor_kspr_ir = std::move(m_cursor_kspr_ir);
    m_rt_screen_w       = m_screen_w;
    m_rt_screen_h       = m_screen_h;
    m_rt_vp_x           = m_vp_x;
    m_rt_vp_y           = m_vp_y;
    if (m_palette_data)
        memcpy(m_rt_palette, m_palette_data, sizeof(m_rt_palette));
    else
        memset(m_rt_palette, 0, sizeof(m_rt_palette));

    // Snapshot the lightmap so the render thread never reads the live game array.
    memcpy(m_rt_lightmap, game.lish.subtile_lightness, sizeof(m_rt_lightmap));

    m_vert_count     = 0;
    m_cmd_vert_start = 0;
    // m_kspr_ir is already empty after std::move above.
}

void GLWorldViewRenderer::ExecuteWorldFromIR(const WorldCommandBuffers& cmds)
{
    ASSERT_RENDER_THREAD();
    GPURenderNow(cmds);
}

void GLWorldViewRenderer::BeginPiPCapture()
{
    // Redirect all geometry writes from draw_view() (called on the render thread
    // for each PiP view) to dedicated pip buffers, so the game thread's world IR /
    // m_draw_cmds / etc. are never written by the render thread concurrently.
    m_pip_capture        = true;
    m_pip_vert_count     = 0;
    m_pip_cmd_vert_start = 0;
    m_pip_draw_cmds.clear();
    m_pip_shadow_cmds.clear();
    m_pip_flatpoly_verts.clear();
    m_pip_kspr_ir.clear();
}

void GLWorldViewRenderer::GPURenderNow(const WorldCommandBuffers& cmds)
{
    KFX_ZONE("WVR::GPURenderNow");
    KFX_GPU_ZONE("Frame::WorldPass");
    KFX_GL_SCOPE(world_pass_dbg, "WorldPass");
    ASSERT_RENDER_THREAD();

    // Reset the per-frame active flag immediately — before any early returns.
    // This ensures m_world_pass_active is false on frames where no
    // BeginWorldPass was issued (e.g. main menu).
    m_world_pass_active = false;

    if (!m_initialized)
    {
        m_rt_draw_cmds.clear();
        m_rt_kspr_ir.clear();
        m_cmd_vert_start = 0;
        return;
    }

    if (m_rt_draw_cmds.empty())
        return;

    const int vp_y_gl = m_full_screen_h - m_rt_vp_y - m_rt_screen_h;
    gpu_execute_passes(m_rt_vp_x, vp_y_gl, m_rt_screen_w, m_rt_screen_h,
                       cmds.tile_verts, cmds.flat_poly_verts, m_rt_kspr_ir);
    m_rt_kspr_ir.clear();
}

void GLWorldViewRenderer::GPURenderToFBO(int pip_w, int pip_h)
{
    KFX_ZONE("WVR::GPURenderToFBO");
    ASSERT_RENDER_THREAD();

    if (!m_initialized)
    {
        m_pip_draw_cmds.clear();
        m_pip_kspr_ir.clear();
        m_pip_verts.clear();
        m_pip_flatpoly_verts.clear();
        m_pip_vert_count     = 0;
        m_pip_cmd_vert_start = 0;
        m_pip_capture        = false;
        return;
    }

    gpu_flush();

    m_rt_draw_cmds      = std::move(m_pip_draw_cmds);
    m_rt_shadow_cmds    = std::move(m_pip_shadow_cmds);
    m_pip_capture        = false;

    if (m_rt_draw_cmds.empty())
    {
        m_pip_kspr_ir.clear();
        m_pip_verts.clear();
        m_pip_flatpoly_verts.clear();
        m_pip_vert_count     = 0;
        m_pip_cmd_vert_start = 0;
        return;
    }

    gpu_execute_passes(0, 0, pip_w, pip_h,
                       m_pip_verts, m_pip_flatpoly_verts, m_pip_kspr_ir);
    m_pip_kspr_ir.clear();
    m_pip_verts.clear();
    m_pip_flatpoly_verts.clear();
    m_pip_vert_count     = 0;
    m_pip_cmd_vert_start = 0;
}

void GLWorldViewRenderer::gpu_execute_passes(int vp_x, int vp_y_gl, int screen_w, int screen_h,
                                             const std::vector<WorldVertex>& tile_verts,
                                             const std::vector<FlatPolyVertex>& fp_verts,
                                             const std::vector<IRWorldKeeperSpriteCmd>& kspr_ir)
{
    ensure_clut_valid();
    m_draw_screen_w = screen_w;
    m_draw_screen_h = screen_h;

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(tile_verts.size() * sizeof(WorldVertex)),
                    tile_verts.data());

    glViewport(vp_x, vp_y_gl, screen_w, screen_h);
    glUseProgram(m_shader);
    glBindVertexArray(m_vao);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    bool atlas_bound       = false;  // tile atlas array bound to GL_TEXTURE0
    bool flatpoly_uploaded = false;  // flat-poly VBO uploaded on first CMD_FLAT_POLYS

    // Push shade/lighting settings uniforms once per flush.
    // This is the authoritative place where g_renderer_settings knobs take effect.
    glUseProgram(m_shader);
    glUniform1f(m_loc_fullbright,    g_renderer_settings.shade_fullbright);
    glUniform1f(m_loc_ambient,       g_renderer_settings.shade_ambient);
    glUniform1f(m_loc_shade_scale,   g_renderer_settings.shade_scale);
    glUniform1f(m_loc_shade_gamma,   std::max(0.0f, g_renderer_settings.shade_gamma));
    glUniform1i(m_loc_lighting_mode, g_renderer_settings.lighting_mode);
    glUniform1i(m_loc_darkness_mode, g_renderer_settings.darkness_mode);
    glUniform1f(m_loc_fog_speed,     g_renderer_settings.fog_speed);
    glUniform1f(m_loc_fog_density,   g_renderer_settings.fog_density);
    // Monotonic time in seconds for fog animation (wraps after ~24 days at float precision)
    {
        static auto t0 = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float secs = std::chrono::duration<float>(now - t0).count();
        glUniform1f(m_loc_time, secs);
    }
    // Keep m_shader bound — pass 1 (tiles) draws with it immediately below.
    // glUseProgram(0) will fuck up flat-poly draws in pass 1, and the flat-poly shader is only used in pass 2 after all tiles are drawn, so we can defer binding it until then.    

    // Upload lightmap shadow copy (snapshotted from game.lish.subtile_lightness[]
    // during FlipBuffers() on the game thread — no live game struct access here).
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_tex_lightmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, k_lightmap_w, k_lightmap_h,
                    GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                    m_rt_lightmap);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);  // restore default

    // Bind fade table to unit 3 unconditionally — the sampler must always
    // reference a valid texture even when darkness_mode != PALETTE, otherwise
    // some drivers produce garbage on the PiP / zoom-box FBO render.
    if (m_fade_tex)
    {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_fade_tex);
    }

    glActiveTexture(GL_TEXTURE0);  // restore default active texture unit

    // Bind palette once for the entire pass — it never changes mid-frame.
    // (Keeper-sprite and shadow passes in pass 2/3 may rebind unit 1, but
    // that's after all pass 1 tile draws are complete.)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);

    // ── Pass 1: Opaque geometry (tiles, flat-colour polys) ──────────────────
    // All opaque commands run first so the depth buffer is fully populated
    // before any blended pass reads it.  Shadows are explicitly excluded here —
    // mixing them with tile batches was causing flickering because tile batches
    // drawn after a shadow command would overdraw the darkened pixels.
    for (const auto& cmd : m_rt_draw_cmds)
    {
        if (cmd.type == DrawCmd::CMD_TILES)
        {
            KFX_GL_PUSH("WorldPass/Tiles");
            if (!atlas_bound)
            {
                GLuint atlas_tex = (m_atlas && m_atlas->IsInitialized())
                                   ? m_atlas->GetAtlasTextureArray()
                                   : 0;
                glActiveTexture(GL_TEXTURE0);
                // Unbind any GL_TEXTURE_2D that may linger on unit 0 from the
                // overhead zoom-box tile pass (which binds a 2D slice of the atlas).
                // Having both a TEXTURE_2D and TEXTURE_2D_ARRAY bound on the same
                // unit is undefined behaviour in core GL 3.3 and causes garbled
                // output on some drivers (notably in FBO renders like the PiP zoom box).
                glBindTexture(GL_TEXTURE_2D, 0);
                glBindTexture(GL_TEXTURE_2D_ARRAY, atlas_tex);
                // When no valid atlas texture is bound the shader would silently
                // output black tiles (palette[0]).  Set the diagnostic flag so the
                // fragment shader renders a magenta/black checkerboard instead,
                // making the missing atlas immediately visible.
                glUniform1f(m_loc_missing_tile, (atlas_tex == 0) ? 1.0f : 0.0f);
                atlas_bound = true;
                // Apply tile filter when the setting has changed.
                int wanted_filter = g_renderer_settings.tile_filter;
                if (wanted_filter != m_tile_filter_applied)
                {
                    GLenum gl_filter = (wanted_filter == 1) ? GL_LINEAR : GL_NEAREST;
                    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, gl_filter);
                    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, gl_filter);
                    glUniform1i(m_loc_tile_filter, wanted_filter);
                    m_tile_filter_applied = wanted_filter;
                }
            }
            glDrawArrays(GL_TRIANGLES, cmd.vert_start, cmd.vert_count);
            KFX_GL_POP();
        }
        else if (cmd.type == DrawCmd::CMD_PRELOAD_KSPR_ATLAS)
        {
            KFX_GL_PUSH("WorldPass/KSprPreload");
            execute_preload_atlas();
            // Restore tile shader and VAO that were active before the preload.
            glUseProgram(m_shader);
            glBindVertexArray(m_vao);
            atlas_bound = false;
            KFX_GL_POP();
        }
        else if (cmd.type == DrawCmd::CMD_FLAT_POLYS)
        {
            KFX_GL_PUSH("WorldPass/FlatPoly");
            // Upload the entire flat-poly buffer once on first encounter, draw sub-range.
            if (!fp_verts.empty())
            {
                if (!flatpoly_uploaded)
                {
                    glBindBuffer(GL_ARRAY_BUFFER, m_flatpoly_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 (GLsizeiptr)(fp_verts.size() * sizeof(FlatPolyVertex)),
                                 fp_verts.data(), GL_STREAM_DRAW);
                    flatpoly_uploaded = true;
                }
                glUseProgram(m_flatpoly_shader);
                glBindVertexArray(m_flatpoly_vao);
                glUniform2f(m_flatpoly_loc_viewport, (float)screen_w, (float)screen_h);
                glDrawArrays(GL_TRIANGLES, cmd.vert_start, cmd.vert_count);
                // Restore tile shader state for subsequent CMD_TILES.
                glUseProgram(m_shader);
                glBindVertexArray(m_vao);
                atlas_bound = false;
            }
            KFX_GL_POP();
        }
    }

    // ── Pass 2: Creature shadows ─────────────────────────────────────────────
    // Shadows are floor-projected quads that multiply-darken the tiles beneath
    // creatures.  Running them after ALL opaque tiles (pass 1) ensures no
    // subsequent tile draw can overwrite the darkened pixels.
    // Depth testing is ENABLED (GL_LEQUAL, no depth write) so that columns and
    // walls correctly occlude shadows — the floor z-values written in pass 1
    // pass the test, while column face z-values are closer and fail it.
    for (const auto& cmd : m_rt_draw_cmds)
    {
        if (cmd.type != DrawCmd::CMD_SHADOWS) continue;

        KFX_GL_PUSH("WorldPass/Shadows");
        assert(cmd.shadow_idx >= 0 && (size_t)cmd.shadow_idx < m_rt_shadow_cmds.size());
        const ShadowCmd& sc = m_rt_shadow_cmds[cmd.shadow_idx];

        // Rasterise the silhouette into the scratch buffer.
        // draw_keepsprite_unscaled_in_buffer handles heap load, FrameOffsW/H
        // placement, and x-flip — matching the software renderer exactly.
        memset(s_kspr_decode_buf, 0, (size_t)sc.tex_h * 256);
        draw_keepsprite_unscaled_in_buffer(sc.anim_sprite, sc.angle,
                                           sc.current_frame, s_kspr_decode_buf);

        // Upload silhouette to the reusable R8 texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_shadow_silhouette_tex);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sc.tex_w, sc.tex_h,
                        GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        // Build 6 float vertices: two triangles covering the quad (0,1,2)+(0,2,3).
        // U/V are 16.16 fixed-point sprite-pixel coords; normalise to [0,1] by /256.
        // No UV x-flip adjustment: draw_keepsprite_unscaled_in_buffer already
        // mirrors the silhouette in the buffer for angles in the flip range.
        float sv[6][4];
        const struct PolyPoint* vp = sc.verts;
        for (int t = 0; t < 6; t++)
        {
            const int idx[6] = { 0, 1, 2, 0, 2, 3 };
            sv[t][0] = (float)vp[idx[t]].X;
            sv[t][1] = (float)vp[idx[t]].Y;
            sv[t][2] = (float)(vp[idx[t]].U >> 16) / 256.0f;
            sv[t][3] = (float)(vp[idx[t]].V >> 16) / 256.0f;
        }
        glBindBuffer(GL_ARRAY_BUFFER, m_shadow_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(sv), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);

        glBindVertexArray(m_shadow_vao);
        glUseProgram(m_shadow_shader);
        glUniform2f(m_shadow_loc_viewport, (float)screen_w, (float)screen_h);
        glUniform1f(m_shadow_loc_darkness, sc.darkness);
        glUniform1f(m_shadow_loc_ndc_z,    sc.ndc_z);

        glDepthMask(GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisable(GL_BLEND);
        glDepthFunc(GL_LEQUAL);  // restore (matches tile-pass default)
        glDepthMask(GL_TRUE);
        KFX_GL_POP();
    }

    // Restore tile shader/VAO state for the sprite pass.
    glUseProgram(m_shader);
    glBindVertexArray(m_vao);
    atlas_bound = false;

    // ── Pass 3: Sprites and world text ───────────────────────────────────────
    // Sprites depth-test against the tile z-buffer written in pass 1 (wall
    // occlusion).  Back-to-front bucket order is preserved because
    // DrawIsometricView recorded CMD_IR_KEEPER_SPRITES entries highest-bucket-first.
    //
    // Scissor-clip sprites to the world viewport so they can't be rasterised
    // into the sidebar region.  The sidebar is painted on top as UI quads,
    // but without a scissor the GPU still rasterises (and alpha-blends)
    // sprites behind it, causing visible bleed-through.
    glEnable(GL_SCISSOR_TEST);
    glScissor(vp_x, vp_y_gl, screen_w, screen_h);

    for (const auto& cmd : m_rt_draw_cmds)
    {
        if (cmd.type == DrawCmd::CMD_IR_KEEPER_SPRITES)
        {
            KFX_GL_PUSH("WorldPass/KeeperSprites");
            glBindVertexArray(0);
            glUseProgram(0);

            UIRenderer_SetScreenSize(screen_w, screen_h);

            const int end = cmd.sprite_ir_start + cmd.sprite_ir_count;
            for (int i = cmd.sprite_ir_start; i < end; ++i)
                DrawKeeperSpriteGL(kspr_ir[i]);

            glUseProgram(m_shader);
            glBindVertexArray(m_vao);
            // Sprite pass leaves unit 1 bound to m_kspr_clut_tex (256×128).
            // The tile shader samples unit 1 as the 256×1 palette — restore it
            // so the next CMD_TILES doesn't sample the wrong row of the CLUT.
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_palette_tex);
            glActiveTexture(GL_TEXTURE0);
            atlas_bound = false;
            KFX_GL_POP();
        }
    }

    UIRenderer_SetScreenSize(m_full_screen_w, m_full_screen_h);

    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);  // sprite pass may leave unit 1/2 active
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glViewport(0, 0, m_full_screen_w, m_full_screen_h);

    // Emit per-frame statistics as Tracy plots.
    KFX_PLOT("WVR/VertCount",          (int)tile_verts.size());
    KFX_PLOT("WVR/DrawCmds",           (int)m_rt_draw_cmds.size());
    KFX_PLOT("WVR/ShadowCmds",         (int)m_rt_shadow_cmds.size());
    KFX_PLOT("WVR/KSprAtlasCacheSize", m_kspr_atlas_used);
    KFX_PLOT("WVR/KSprAtlasHits",      m_kspr_atlas_hits);
    KFX_PLOT("WVR/KSprAtlasMisses",    m_kspr_atlas_misses);
    KFX_PLOT("WVR/KSprAtlasPeak",      m_kspr_atlas_peak);

    // Reset render-side buffers for the next frame.
    m_rt_draw_cmds.clear();
    m_rt_shadow_cmds.clear();
    m_cmd_vert_start = 0;
}

/******************************************************************************/

void GLWorldViewRenderer::DrawIsometricView()
{
    KFX_ZONE("WVR::DrawIsometricView");
    ASSERT_GAME_THREAD();
    if (!m_initialized) {
        ERRORLOG("GLWorldViewRenderer asked to draw ISO but not initialised — frame dropped");
        return;
    }

    // Use pip-specific command/shadow/flatpoly lists when capturing for PiP,
    // otherwise use the normal game-thread write buffers.
    if (!m_pip_capture && !m_world_write_cmds)
        return;

    std::vector<DrawCmd>&        draw_cmds   = m_pip_capture ? m_pip_draw_cmds      : m_draw_cmds;
    std::vector<ShadowCmd>&      shadow_cmds = m_pip_capture ? m_pip_shadow_cmds    : m_shadow_cmds;
    std::vector<FlatPolyVertex>& fpverts     = m_pip_capture ? m_pip_flatpoly_verts : m_world_write_cmds->flat_poly_verts;

    // Walk the depth-sorted bucket list back-to-front (painter's algorithm).
    // For each bucket:
    //   Geometry types are accumulated in the VBO (batched across buckets for
    //   efficiency).  When a bucket contains 3D entity sprites (JontySprite /
    //   JontyISOSprite), the accumulated tile geometry is flushed to GL first,
    //   the sprite draw functions are called (which queue quads to the GPU
    //   sprite backend), then the sprite quads are immediately flushed.
    // Non-spatial elements (shadows, selector, status, text, room flags) are
    // drawn afterwards into the CPU staging buffer via draw_nonspatial_sprites().
    // bi >= 0: the GPU renderer must include bucket 0 (tiles with z < 32 in
    // fill_in_points_isometric are clamped to z=0 → bucket_index=0).  The
    // software renderer skips bucket 0 because its scan-converter can't handle
    // near-plane vertices, but OpenGL clips natively so they're always safe.
    for (int bi = BUCKETS_COUNT - 1; bi >= 0; bi--)
    {
        m_current_bucket = bi;
        bool bucket_has_3d_sprites = false;
        bool bucket_has_flat_polys = false;
        const int flatpoly_vert_start = (int)fpverts.size();

        struct BasicQ* q = buckets[bi];
        while (q != nullptr)
        {
            switch (q->kind)
            {
                // ── Full PolyPoint (fixed-point 16:16) geometry ─────────────
                case QK_PolygonStandard:
                {
                    auto* p = (struct BucketKindPolygonStandard*)q;
                    append_triangle(p->block,
                                    &p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third,
                                    p->camera_z_first,
                                    p->camera_z_second,
                                    p->camera_z_third);
                    break;
                }
                case QK_PolygonSimple:
                {
                    auto* p = (struct BucketKindPolygonSimple*)q;
                    append_triangle(p->block,
                                    &p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third);
                    break;
                }
                case QK_PolygonNearFP:
                {
                    auto* p = (struct BucketKindPolygonNearFP*)q;
                    append_triangle(p->block,
                                    &p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third,
                                    p->coordinate_first.z,
                                    p->coordinate_second.z,
                                    p->coordinate_third.z);
                    break;
                }
                // ── Compact types (unsigned short x/y, unsigned char uv) ────
                case QK_TrigMode2:
                {
                    auto* p = (struct BucketKindTrigMode2*)q;
                    append_triangle_compact(
                        p->vertex_first_x,  p->vertex_first_y,  p->texture_u_first,  p->texture_v_first,  255,
                        p->vertex_second_x, p->vertex_second_y, p->texture_u_second, p->texture_v_second, 255,
                        p->vertex_third_x,  p->vertex_third_y,  p->texture_u_third,  p->texture_v_third,  255);
                    break;
                }
                case QK_TrigMode3:
                {
                    auto* p = (struct BucketKindTrigMode3*)q;
                    append_triangle_compact(
                        p->vertex_first_x,  p->vertex_first_y,  p->texture_u_first,  p->texture_v_first,  255,
                        p->vertex_second_x, p->vertex_second_y, p->texture_u_second, p->texture_v_second, 255,
                        p->vertex_third_x,  p->vertex_third_y,  p->texture_u_third,  p->texture_v_third,  255);
                    break;
                }
                case QK_TrigMode6:
                {
                    auto* p = (struct BucketKindTrigMode6*)q;
                    append_triangle_compact(
                        p->vertex_first_x,  p->vertex_first_y,  p->texture_u_first,  p->texture_v_first,  p->texture_w_first,
                        p->vertex_second_x, p->vertex_second_y, p->texture_u_second, p->texture_v_second, p->texture_w_second,
                        p->vertex_third_x,  p->vertex_third_y,  p->texture_u_third,  p->texture_v_third,  p->texture_w_third);
                    break;
                }
                case QK_PolyMode5:
                {
                    auto* p = (struct BucketKindPolyMode5*)q;
                    append_triangle_compact(
                        p->vertex_first_x,  p->vertex_first_y,  p->texture_u_first,  p->texture_v_first,  p->texture_w_first,
                        p->vertex_second_x, p->vertex_second_y, p->texture_u_second, p->texture_v_second, p->texture_w_second,
                        p->vertex_third_x,  p->vertex_third_y,  p->texture_u_third,  p->texture_v_third,  p->texture_w_third);
                    break;
                }

                // ── Flat-colour types: defer to CPU via staging buffer ───────
                case QK_PolyMode0:
                case QK_PolyMode4:
                case QK_BasicPolygon:
                    bucket_has_flat_polys = true;
                    break;

                // ── 3D entity sprites: mark for depth-correct flush below ────
                case QK_JontySprite:
                case QK_JontyISOSprite:
                    bucket_has_3d_sprites = true;
                    break;

                // ── Creature shadows: GPU multiply-blend over rendered tiles ─
                case QK_CreatureShadow:
                {
                    auto* s = (struct BucketKindCreatureShadow*)q;
                    struct KeeperSprite* kspr_arr = keepersprite_array(s->anim_sprite);
                    if (!kspr_arr || kspr_arr->FramesCount == 0) break;

                    // Resolve frame dimensions at collection time.  Heap load,
                    // offset placement, and x-flip are all handled by
                    // draw_keepsprite_unscaled_in_buffer at flush time.
                    short angle         = (short)s->angle;
                    int   quarter       = abs(4 - (((angle + DEGREES_22_5) & ANGLE_MASK) >> 8));
                    unsigned char frame = s->current_frame;
                    if (frame >= kspr_arr->FramesCount)
                        frame = kspr_arr->FramesCount - 1;

                    int tex_w, tex_h;
                    if (kspr_arr->Rotable == 0) {
                        struct KeeperSprite* ks = &kspr_arr[frame];
                        tex_w = ks->FrameWidth;
                        tex_h = ks->FrameHeight;
                    } else if (kspr_arr->Rotable == 2) {
                        struct KeeperSprite* ks = &kspr_arr[frame + quarter * kspr_arr->FramesCount];
                        tex_w = ks->SWidth;
                        tex_h = ks->SHeight;
                    } else {
                        break;  // unsupported rotable type
                    }
                    if (tex_w <= 0 || tex_h <= 0 || tex_w > 256 || tex_h > 256) break;

                    ShadowCmd sc;
                    sc.verts[0]      = s->vertex_first;
                    sc.verts[1]      = s->vertex_second;
                    sc.verts[2]      = s->vertex_third;
                    sc.verts[3]      = s->vertex_fourth;
                    sc.anim_sprite   = s->anim_sprite;
                    sc.angle         = angle;
                    sc.current_frame = frame;
                    sc.tex_w         = tex_w;
                    sc.tex_h         = tex_h;
                    // vertex_first.S holds dist_sq (range 16..31) set by create_shadows().
                    sc.darkness      = 1.0f - (float)s->vertex_first.S / 32.0f;
                    sc.ndc_z         = 2.0f * (float)bi / (float)(BUCKETS_COUNT - 1) - 1.0f;
                    DrawCmd cmd;
                    cmd.type       = DrawCmd::CMD_SHADOWS;
                    cmd.shadow_idx = (int)shadow_cmds.size();
                    shadow_cmds.push_back(sc);
                    draw_cmds.push_back(cmd);
                    break;
                }

                // ── All other types go to draw_nonspatial_sprites() ──────────
                default:
                    break;
            }
            q = q->next;
        }

        // Commit accumulated tile geometry as a batch command, then walk the
        // bucket's sprite list on the game thread (Option A).
        // The walk calls SubmitKeeperSprite() which appends IRWorldKeeperSpriteCmd
        // entries to kspr_buf; we record a CMD_IR_KEEPER_SPRITES referencing those.
        if (bucket_has_3d_sprites)
        {
            gpu_flush();

            auto& kspr_buf  = m_pip_capture ? m_pip_kspr_ir : m_kspr_ir;
            setup_world_sprite_processing(bi);
            const int ir_start = (int)kspr_buf.size();
            draw_3d_sprites_for_bucket(bi);
            const int ir_count = (int)kspr_buf.size() - ir_start;
            if (ir_count > 0)
            {
                DrawCmd cmd;
                cmd.type             = DrawCmd::CMD_IR_KEEPER_SPRITES;
                cmd.sprite_ir_start  = ir_start;
                cmd.sprite_ir_count  = ir_count;
                draw_cmds.push_back(cmd);
            }
        }

        // Flat-colour polygons: GPU triangle draw via flat-poly shader.
        if (bucket_has_flat_polys)
        {
            gpu_flush();

            DrawCmd cmd;
            cmd.type       = DrawCmd::CMD_FLAT_POLYS;
            cmd.vert_start = flatpoly_vert_start;
            cmd.vert_count = (int)fpverts.size() - flatpoly_vert_start;
            draw_cmds.push_back(cmd);
        }
    }

    // Commit any trailing tile geometry that was not closed by a sprite or
    // flat-poly interrupt in the last bucket.  Mirrors the identical call in
    // DrawFrontView() (line ~2015).  This MUST happen before FlipBuffers() so
    // that the game-thread m_draw_cmds contains all CMD_TILES entries and
    // m_vert_count == m_cmd_vert_start (no open batch) at flip time.
    gpu_flush();

    // draw_nonspatial_sprites_gpu() is intentionally NOT called here.
    // It is called from engine_render.c immediately after WorldViewRenderer_DrawIsometricView(),
    // guarded by cam->view_mode so it only runs for isometric/creature views (not parchment map).
}

bool GLWorldViewRenderer::append_frontview_quad(const struct BucketKindTexturedQuad* txquad)
{
    // Mirror draw_texturedquad_block(): build 4 PolyPoints then submit as 2 triangles.
    // orient_to_mapU/V values are 16:16 fixed-point (0 or 0x1F0000); >> 16 = 0 or 31,
    // matching the U >> 16 convention already used by append_triangle().
    int orient = txquad->orient & 3;

    PolyPoint a, d, b, c;
    a.X = (txquad->texture_x)          >> 8;
    a.Y = (txquad->texture_y)          >> 8;
    a.U = orient_to_mapU1[orient];
    a.V = orient_to_mapV1[orient];
    a.S = txquad->shade_intensity0;

    d.X = (txquad->texture_x + txquad->zoom_x) >> 8;
    d.Y = (txquad->texture_y)                  >> 8;
    d.U = orient_to_mapU2[orient];
    d.V = orient_to_mapV2[orient];
    d.S = txquad->shade_intensity1;

    b.X = (txquad->texture_x + txquad->zoom_x) >> 8;
    b.Y = (txquad->texture_y + txquad->zoom_y) >> 8;
    b.U = orient_to_mapU3[orient];
    b.V = orient_to_mapV3[orient];
    b.S = txquad->shade_intensity2;

    c.X = (txquad->texture_x)                  >> 8;
    c.Y = (txquad->texture_y + txquad->zoom_y) >> 8;
    c.U = orient_to_mapU4[orient];
    c.V = orient_to_mapV4[orient];
    c.S = txquad->shade_intensity3;

    int tile_id;
    switch (txquad->marked_mode)
    {
        case 0:  tile_id = TEXTURE_LAND_MARKED_LAND; break;
        case 1:  tile_id = TEXTURE_LAND_MARKED_GOLD; break;
        default: tile_id = (int)txquad->texture_idx; break;
    }

    // Front view uses orthographic projection — camera_z defaults to 1.0f (affine).
    bool ok = append_triangle(tile_id, &a, &d, &b);
    ok     &= append_triangle(tile_id, &a, &b, &c);
    return ok;
}

void GLWorldViewRenderer::DrawFrontView(struct Camera* cam)
{
    KFX_ZONE("WVR::DrawFrontView");
    ASSERT_GAME_THREAD();
    if (!m_initialized)
        return;

    std::vector<DrawCmd>& draw_cmds = m_pip_capture ? m_pip_draw_cmds : m_draw_cmds;

    // Walk the front-view bucket list back-to-front (painter's algorithm).
    // Front view populates buckets with QK_TextureQuad (floor/wall/ceiling tiles)
    // and creature/thing sprites.  All other overlay types (status flowers, gold text,
    // room flags, slab selector) are handled by draw_nonspatial_sprites_gpu() which
    // the caller (draw_frontview_engine) already invokes after this function.
    for (int bi = BUCKETS_COUNT - 1; bi >= 0; bi--)
    {
        m_current_bucket = bi;
        bool bucket_has_sprites = false;

        for (struct BasicQ* q = buckets[bi]; q != nullptr; q = q->next)
        {
            switch (q->kind)
            {
                case QK_TextureQuad:
                    append_frontview_quad(reinterpret_cast<const struct BucketKindTexturedQuad*>(q));
                    break;

                case QK_JontySprite:
                case QK_JontyISOSprite:
                    bucket_has_sprites = true;
                    break;

                default:
                    break;
            }
        }

        if (bucket_has_sprites)
        {
            gpu_flush();

            auto& kspr_buf  = m_pip_capture ? m_pip_kspr_ir : m_kspr_ir;
            setup_world_sprite_processing(bi);
            const int ir_start = (int)kspr_buf.size();
            draw_frontview_3d_sprites_for_bucket_current(bi);
            const int ir_count = (int)kspr_buf.size() - ir_start;
            if (ir_count > 0)
            {
                DrawCmd cmd;
                cmd.type             = DrawCmd::CMD_IR_KEEPER_SPRITES;
                cmd.sprite_ir_start  = ir_start;
                cmd.sprite_ir_count  = ir_count;
                draw_cmds.push_back(cmd);
            }
        }
    }

    gpu_flush(); // commit any trailing tile geometry
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
