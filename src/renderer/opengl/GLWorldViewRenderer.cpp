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
#include "renderer/opengl/GLShaderLoader.h"
#include "renderer/opengl/GLTextRenderer.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/ITileAtlas.h"
#include "renderer/RendererManager.h"  // for RendererGetActive/RendererGetActiveType
#include "renderer/RendererOpenGL.h"   // for RendererOpenGL class

#include "engine_buckets.h"   // QKinds enum, BasicQ, BucketKind* structs, buckets[]
#include "engine_textures.h"  // TEXTURE_BLOCKS_COUNT
#include "renderer/TileAtlasPacker.h" // GetTileUV
#include "engine_render.h"    // draw_3d_sprites_for_bucket(), draw_nonspatial_sprites_no_shadows(), draw_keepsprite_unscaled_in_buffer()
#include "bflib_render.h"      // PolyPoint, render_fade_tables
#include "bflib_video.h"       // MyScreenWidth, MyScreenHeight
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG / WARNLOG
#include "front_simple.h"      // big_scratch (256x256 silhouette scratch buffer)
#include "renderer/RenderPass_C.h" // RenderPass_FlushNow()
#include "renderer/RenderPass.h"    // RenderPassSystem::SetScreenSize()
#include "renderer/backends/OpenGLSpriteBackend.h" // SetCurrentBucketZ()
#include "creature_graphics.h" // KeeperSprite structure
#include "bflib_sprite.h"      // TbSprite structure

#include <glad/glad.h>
#include <cstdlib>
#include <cstring>
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

/** Scratch buffer used by render_keepersprite_gpu() to hold decoded palette indices.
 *  Stride is always 256 so glTexSubImage2D can use GL_UNPACK_ROW_LENGTH. */
static uint8_t s_kspr_decode_buf[256 * 256];

/** Decode keeper-sprite RLE into a stride-256 palette-index buffer.
 *  Format is identical to TbSprite.Data: negative cmd = transparent skip,
 *  positive cmd = run of palette bytes, 0 = end of row. */
static void decode_keeper_rle(uint8_t* dst, const uint8_t* data, int w, int h)
{
    for (int y = 0; y < h; ++y)
        memset(dst + y * 256, 0, w);

    const signed char* sp = reinterpret_cast<const signed char*>(data);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + y * 256;
        int x = 0;
        while (true) {
            signed char cmd = *sp++;
            if (cmd == 0) break;
            if (cmd < 0) {
                x += (int)(-cmd);
            } else {
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
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
    m_sw_fallback = new SoftwareWorldViewRenderer();
    m_text_renderer = new GLTextRenderer();
}

GLWorldViewRenderer::~GLWorldViewRenderer()
{
    free_gl_resources();
    delete m_sw_fallback;
    delete m_text_renderer;
}

/******************************************************************************/

bool GLWorldViewRenderer::init_gl_resources()
{
    if (m_initialized)
        return true;

    if (!compile_world_shaders())
        return false;

    // VAO + dynamic VBO for world geometry
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

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

    glBindVertexArray(0);

    // CPU staging buffer
    m_verts = (WorldVertex*)malloc(k_max_verts * sizeof(WorldVertex));
    if (!m_verts)
    {
        ERRORLOG("GLWorldViewRenderer: failed to allocate vertex staging buffer");
        free_gl_resources();
        return false;
    }

    // Cache uniform locations and bind samplers to fixed texture units
    glUseProgram(m_shader);
    m_loc_tile_atlas = glGetUniformLocation(m_shader, "u_tile_atlas");
    glUniform1i(m_loc_tile_atlas, 0);   // GL_TEXTURE0 — R8 palette-index atlas
    m_loc_palette = glGetUniformLocation(m_shader, "u_palette");
    glUniform1i(m_loc_palette, 1);      // GL_TEXTURE1 — 1D RGBA8 palette
    glUseProgram(0);

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

    // Initialize text renderer for world-space text
    if (m_text_renderer && !m_text_renderer->Init())
    {
        ERRORLOG("GLWorldViewRenderer: failed to initialise text renderer");
        free_gl_resources();
        return false;
    }

    m_initialized = true;
    SYNCLOG("GLWorldViewRenderer: initialised (VBO %d verts × %zu bytes)",
            k_max_verts, sizeof(WorldVertex));
    return true;
}

void GLWorldViewRenderer::free_gl_resources()
{
    free(m_verts);
    m_verts = nullptr;

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
    if (m_kspr_sprite_tex)  { glDeleteTextures(1, &m_kspr_sprite_tex);      m_kspr_sprite_tex = 0; }
    if (m_kspr_palette_tex) { glDeleteTextures(1, &m_kspr_palette_tex);     m_kspr_palette_tex = 0; }

    if (m_flatpoly_vao)    { glDeleteVertexArrays(1, &m_flatpoly_vao); m_flatpoly_vao = 0; }
    if (m_flatpoly_vbo)    { glDeleteBuffers(1, &m_flatpoly_vbo);       m_flatpoly_vbo = 0; }
    if (m_flatpoly_shader) { glDeleteProgram(m_flatpoly_shader);        m_flatpoly_shader = 0; }

    if (m_text_renderer)    { m_text_renderer->Shutdown(); }

    m_initialized = false;
}

bool GLWorldViewRenderer::compile_world_shaders()
{
    std::string vert_src = get_embedded_shader_source("world_vert.glsl");
    std::string frag_src = get_embedded_shader_source("world_frag.glsl");
    if (vert_src.empty() || frag_src.empty())
        return false;

    GLuint vert = compile_shader_src(GL_VERTEX_SHADER,   vert_src.c_str(), "world_vert.glsl");
    GLuint frag = compile_shader_src(GL_FRAGMENT_SHADER, frag_src.c_str(), "world_frag.glsl");
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
    return true;
}

bool GLWorldViewRenderer::init_shadow_shader()
{
    std::string sv_src = get_embedded_shader_source("shadow_vert.glsl");
    std::string sf_src = get_embedded_shader_source("shadow_frag.glsl");
    if (sv_src.empty() || sf_src.empty())
        return false;

    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   sv_src.c_str(), "shadow_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, sf_src.c_str(), "shadow_frag.glsl");
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
    glUniform1i(m_shadow_loc_silhouette, 0); // GL_TEXTURE0
    glUseProgram(0);

    // Shadow quad VAO + VBO: 6 vertices × (vec2 pos + vec2 uv) = 4 floats each
    glGenVertexArrays(1, &m_shadow_vao);
    glGenBuffers(1, &m_shadow_vbo);
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
    glBindTexture(GL_TEXTURE_2D, m_shadow_silhouette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool GLWorldViewRenderer::init_keeper_sprite_shader()
{
    std::string sv_src = get_embedded_shader_source("kspr_vert.glsl");
    std::string sf_src = get_embedded_shader_source("kspr_frag.glsl");
    if (sv_src.empty() || sf_src.empty())
        return false;

    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   sv_src.c_str(), "kspr_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, sf_src.c_str(), "kspr_frag.glsl");
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
        std::string gf_src = get_embedded_shader_source("kspr_glow_frag.glsl");
        if (gf_src.empty())
        {
            ERRORLOG("GLWorldViewRenderer: failed to load glow shaders");
            return false;
        }
        GLuint gv = compile_shader_src(GL_VERTEX_SHADER,   sv_src.c_str(),    "kspr_vert.glsl");
        GLuint gf = compile_shader_src(GL_FRAGMENT_SHADER, gf_src.c_str(), "kspr_glow_frag.glsl");
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

    // Reusable 256x256 R8 texture — overwritten per sprite
    glGenTextures(1, &m_kspr_sprite_tex);
    glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, 256, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 256x1 RGBA8 palette LUT
    glGenTextures(1, &m_kspr_palette_tex);
    glBindTexture(GL_TEXTURE_2D, m_kspr_palette_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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

    SYNCLOG("GLWorldViewRenderer: keeper-sprite shader initialised");
    return true;
}

bool GLWorldViewRenderer::init_flatpoly_shader()
{
    std::string sv_src = get_embedded_shader_source("flatpoly_vert.glsl");
    std::string sf_src = get_embedded_shader_source("flatpoly_frag.glsl");
    if (sv_src.empty() || sf_src.empty())
        return false;

    GLuint sv = compile_shader_src(GL_VERTEX_SHADER,   sv_src.c_str(), "flatpoly_vert.glsl");
    GLuint sf = compile_shader_src(GL_FRAGMENT_SHADER, sf_src.c_str(), "flatpoly_frag.glsl");
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

    // VAO + dynamic VBO: 6 floats per vertex (x, y, z, r, g, b)
    glGenVertexArrays(1, &m_flatpoly_vao);
    glGenBuffers(1, &m_flatpoly_vbo);
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

void GLWorldViewRenderer::BeginHandSpriteRendering()
{
    // Save current viewport params used by render_keepersprite_gpu()
    m_saved_screen_w  = m_screen_w;
    m_saved_screen_h  = m_screen_h;
    m_saved_sprite_z  = m_current_sprite_z;

    // Hand sprites are at mouse position in full-screen pixel coordinates.
    // Override viewport to full screen so the kspr shader converts them correctly.
    m_screen_w = (int)MyScreenWidth;
    m_screen_h = (int)MyScreenHeight;

    // z = -1.0 maps to depth 0.0 (near plane); always passes GL_LEQUAL against
    // any world geometry that was written at depth >= 0.0.
    m_current_sprite_z = -1.0f;

    // Ensure the GL viewport covers the full window (same as post-GPUFlushNow state).
    glViewport(0, 0, (int)MyScreenWidth, (int)MyScreenHeight);

    // Hand sprites must always appear on top — disable depth testing so world-pass
    // depth values never cull them.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
}

void GLWorldViewRenderer::EndHandSpriteRendering()
{
    m_screen_w         = m_saved_screen_w;
    m_screen_h         = m_saved_screen_h;
    m_current_sprite_z = m_saved_sprite_z;
}

/******************************************************************************/

int GLWorldViewRenderer::SubmitKeeperSprite(
    long dst_x, long dst_y, long dst_w, long dst_h,
    const unsigned char* data, int src_w, int src_h,
    unsigned int draw_flags, const unsigned char* remap)
{
    return render_keepersprite_gpu(dst_x, dst_y, dst_w, dst_h,
                                   data, src_w, src_h, draw_flags, remap);
}

/******************************************************************************/

int GLWorldViewRenderer::render_keepersprite_gpu(
    long dst_x, long dst_y, long dst_w, long dst_h,
    const unsigned char* data, int src_w, int src_h,
    unsigned int draw_flags, const unsigned char* remap)
{
    if (!m_kspr_shader || !m_kspr_sprite_tex || !m_kspr_palette_tex) return 0;
    if (src_w <= 0 || src_h <= 0 || src_w > 256 || src_h > 256)      return 0;
    if (dst_w <= 0 || dst_h <= 0)                                      return 0;

    // Upload palette on first sprite of this frame
    if (m_kspr_palette_dirty)
    {
        uint8_t rgba[256 * 4];
        for (int i = 0; i < 256; ++i)
        {
            rgba[i*4+0] = (uint8_t)((int)lbPalette[i*3+0] << 2);
            rgba[i*4+1] = (uint8_t)((int)lbPalette[i*3+1] << 2);
            rgba[i*4+2] = (uint8_t)((int)lbPalette[i*3+2] << 2);
            rgba[i*4+3] = 255;
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_kspr_palette_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        m_kspr_palette_dirty = false;
    }

    // Decode RLE into the stride-256 scratch buffer
    decode_keeper_rle(s_kspr_decode_buf, data, src_w, src_h);

    // Apply colour remap when Lb_TEXT_UNDERLNSHADOW flag is set (white/red highlight,
    // shade fade, tint).  Additive glow sprites are excluded: their raw pixels are
    // glow-encoding indices (1–64) fed directly to the glow shader, not palette indices.
    if (remap && (draw_flags & Lb_TEXT_UNDERLNSHADOW) && !(draw_flags & Lb_SPRITE_ALPHA_ADDITIVE))
    {
        for (int y = 0; y < src_h; ++y) {
            uint8_t* row = s_kspr_decode_buf + y * 256;
            for (int x = 0; x < src_w; ++x)
                if (row[x] != 0) row[x] = remap[row[x]];
        }
    }

    // Upload decoded sprite (stride=256 in CPU buffer, upload src_w×src_h region)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 256);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w, src_h,
                    GL_RED, GL_UNSIGNED_BYTE, s_kspr_decode_buf);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    // UV covers only the used src_w×src_h region of the 256×256 texture
    float u1 = (float)src_w / 256.0f;
    float v1 = (float)src_h / 256.0f;
    float ul = 0.0f, ur = u1;
    if (draw_flags & Lb_SPRITE_FLIP_HORIZ) { ul = u1; ur = 0.0f; }

    float vx0 = (float)dst_x,         vy0 = (float)dst_y;
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
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);

    // Lb_SPRITE_ALPHA_ADDITIVE — fire/glow effects.
    // Sprite pixels 1–64 are DK glow-encoding indices; the glow shader decodes
    // each pixel's colour family and intensity row into a direct additive RGB delta
    // (matching compute_alpha_tables()), drawn with GL_ONE/GL_ONE so the delta is
    // added to whatever player-coloured geometry was rendered beneath.
    const bool additive = (draw_flags & Lb_SPRITE_ALPHA_ADDITIVE) != 0;
    float alpha = 1.0f;
    if      (draw_flags & Lb_SPRITE_TRANSPAR4) alpha = 0.5f;
    else if (draw_flags & Lb_SPRITE_TRANSPAR8) alpha = 0.25f;

    glBindVertexArray(m_kspr_vao);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    if (additive && m_kspr_glow_shader)
    {
        // Pure additive blend: adds glow RGB delta to framebuffer contents.
        glUseProgram(m_kspr_glow_shader);
        glUniform2f(m_kspr_glow_loc_viewport, (float)m_screen_w, (float)m_screen_h);
        glUniform1f(m_kspr_glow_loc_z_ndc,    m_current_sprite_z);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);

        glBlendFunc(GL_ONE, GL_ONE);
    }
    else
    {
        glUseProgram(m_kspr_shader);
        glUniform2f(m_kspr_loc_viewport, (float)m_screen_w, (float)m_screen_h);
        glUniform1f(m_kspr_loc_alpha,    alpha);
        glUniform1f(m_kspr_loc_z_ndc,    m_current_sprite_z);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_kspr_palette_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_kspr_sprite_tex);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    glUseProgram(0);

    return 1;
}

void GLWorldViewRenderer::AddWorldText(float world_x, float world_y, float world_z,
                                      const char* text, int color, float scale, int bucket_num)
{
    if (!text || !m_initialized)
        return;

    // Calculate NDC depth from bucket number (same as sprite/tile depth calculation)
    float z_ndc = 2.0f * (float)bucket_num / (float)(BUCKETS_COUNT - 1) - 1.0f;

    WorldTextCmd cmd;
    cmd.world_x = world_x;
    cmd.world_y = world_y;
    cmd.world_z = world_z;
    cmd.ndc_z = z_ndc;
    cmd.text = text;  // Note: caller must ensure text remains valid until GPUFlushNow()
    cmd.bucket_num = bucket_num;
    cmd.color = color;
    cmd.scale = scale;

    m_worldtext_cmds.push_back(cmd);

    // Add a draw command to render this text at the appropriate depth
    DrawCmd draw_cmd;
    draw_cmd.type = DrawCmd::CMD_WORLDTEXT;
    draw_cmd.worldtext_idx = (int)(m_worldtext_cmds.size() - 1);
    m_draw_cmds.push_back(draw_cmd);
}

/******************************************************************************/

// Simple bridge function to maintain existing sprite logic while using proper abstraction
void GLWorldViewRenderer::setup_world_sprite_processing(long bucket_num)
{
    if (!m_initialized) return;

    // Set up depth for this bucket
    const float sprite_z = 2.0f * (float)bucket_num / (float)(BUCKETS_COUNT - 1) - 1.0f;
    OpenGLSpriteBackend::SetCurrentBucketZ(sprite_z);

    // Store the depth for keeper sprite rendering
    m_current_sprite_z = sprite_z;
}

/******************************************************************************/

void GLWorldViewRenderer::BeginWorldPass(unsigned char* framebuf, int pitch,
                                          int w, int h, int vp_x, int vp_y)
{
    m_screen_w        = w;
    m_screen_h        = h;
    m_vp_x            = vp_x;
    m_vp_y            = vp_y;
    m_framebuf        = framebuf;
    m_pitch           = pitch;
    m_vert_count      = 0;
    m_cmd_vert_start  = 0;
    m_current_variation = 0;
    m_current_bucket    = 0;
    m_draw_cmds.clear();
    m_shadow_cmds.clear();
    m_worldtext_cmds.clear();
    m_kspr_palette_dirty = true;

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

    // Zero the viewport area in the CPU staging buffer so palette index 0
    // acts as transparent in the compositing blit, letting GPU-rendered tiles
    // show through.
    //
    // Only zero when the GPU tile atlas is actually ready.  If the atlas hasn't
    // initialised yet this frame, leave the staging buffer intact so software-
    // rendered tiles (written by the CPU path) remain visible rather than going
    // black.
    //
    // Clear only the exact viewport region to avoid interference with UI elements
    // that may have been rendered outside the viewport bounds (like sidebar).
    // The framebuf pointer is already offset to the viewport area by LbScreenSetGraphicsWindow.
    const bool atlas_ready = m_atlas && m_atlas->IsInitialized();
    if (framebuf && atlas_ready)
    {
        for (int row = 0; row < h; row++)
            memset(framebuf + (long)row * pitch, 0, (size_t)w);
    }

    // Set screen size for text renderer
    if (m_text_renderer)
        m_text_renderer->SetScreenSize(w, h);

    // Software fallback always receives the pass too (it sets vec globals)
    if (m_sw_fallback)
        m_sw_fallback->BeginWorldPass(framebuf, pitch, w, h, 0, 0);
}

/******************************************************************************/

bool GLWorldViewRenderer::append_triangle(int tile_id,
                                           const struct PolyPoint* p0,
                                           const struct PolyPoint* p1,
                                           const struct PolyPoint* p2)
{
    const int variation  = tile_id / TEXTURE_BLOCKS_COUNT;
    const int tile_local = tile_id % TEXTURE_BLOCKS_COUNT;

    // Flush the current batch whenever the atlas variation changes so each
    // draw call uses a single consistent texture.
    if (variation != m_current_variation)
    {
        gpu_flush();
        m_current_variation = variation;
    }

    if (m_vert_count + 3 > k_max_verts)
        gpu_flush();

    // Look up the normalised UV rectangle for this tile in the atlas.
    float u0f, v0f, u1f, v1f;
    TileAtlasPacker::GetTileUV(tile_local, &u0f, &v0f, &u1f, &v1f);

    // Derive NDC depth from the painter's-algorithm bucket index:
    //   high bi (far away) → z near +1.0; low bi (close) → z near -1.0.
    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;

    const struct PolyPoint* pts[3] = { p0, p1, p2 };
    for (int i = 0; i < 3; i++)
    {
        WorldVertex* wv = &m_verts[m_vert_count + i];
        // X/Y are integer screen pixels (not 16:16 fixed-point).
        wv->x = (float)(pts[i]->X) / (float)m_screen_w * 2.0f - 1.0f;
        wv->y = 1.0f - (float)(pts[i]->Y) / (float)m_screen_h * 2.0f;
        wv->z = z_ndc;
        // U/V are 16:16; integer part (>>16) is texel 0..31 within the 32-px tile.
        wv->u = u0f + ((float)(pts[i]->U >> 16) / 32.0f) * (u1f - u0f);
        wv->v = v0f + ((float)(pts[i]->V >> 16) / 32.0f) * (v1f - v0f);
        // S = shade_intensity<<8; (S>>16) gives shade level 0..63.
        wv->shade = (float)(pts[i]->S >> 16) / 63.0f;
    }
    m_vert_count += 3;
    return true;
}

bool GLWorldViewRenderer::append_triangle_compact(
    int sx0, int sy0, int u0, int v0, int shade0,
    int sx1, int sy1, int u1, int v1, int shade1,
    int sx2, int sy2, int u2, int v2, int shade2)
{
    if (m_vert_count + 3 > k_max_verts)
        gpu_flush();

    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;
    WorldVertex* v = &m_verts[m_vert_count];
    COMPACT_UV_TO_WORLDVERTEX(&v[0], sx0, sy0, u0, v0, shade0, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[1], sx1, sy1, u1, v1, shade1, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[2], sx2, sy2, u2, v2, shade2, m_screen_w, m_screen_h);
    v[0].z = z_ndc; v[1].z = z_ndc; v[2].z = z_ndc;
    m_vert_count += 3;
    return true;
}

void GLWorldViewRenderer::gpu_flush()
{
    if (!m_initialized || m_vert_count <= m_cmd_vert_start)
        return;

    // Record the current tile batch as a deferred draw command.
    // No GL calls are issued here — everything is replayed in GPUFlushNow()
    // AFTER RendererOpenGL::EndFrame() calls glClear().
    DrawCmd cmd;
    cmd.type       = DrawCmd::CMD_TILES;
    cmd.vert_start = m_cmd_vert_start;
    cmd.vert_count = m_vert_count - m_cmd_vert_start;
    cmd.variation  = m_current_variation;
    m_draw_cmds.push_back(cmd);
    m_cmd_vert_start = m_vert_count;
}

void GLWorldViewRenderer::GPUFlushNow(unsigned char* staging_buf)
{
    if (!m_initialized)
    {
        m_draw_cmds.clear();
        m_vert_count     = 0;
        m_cmd_vert_start = 0;
        return;
    }

    // Flush any tile batch that hasn't been recorded yet.
    gpu_flush();

    if (m_draw_cmds.empty())
        return;

    // Upload the full vertex buffer once (all batches are already packed in order).
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(m_vert_count * sizeof(WorldVertex)),
                    m_verts);

    // Execute all draw commands in submission order, interleaving tile batches
    // and 3D sprite flushes to maintain the painter's-algorithm depth order.
    const int vp_y_gl = (int)MyScreenHeight - m_vp_y - m_screen_h;
    glViewport(m_vp_x, vp_y_gl, m_screen_w, m_screen_h);
    glUseProgram(m_shader);
    glBindVertexArray(m_vao);

    // Enable hardware depth testing so billboarded sprites are correctly
    // occluded by world geometry and vice-versa.  GL_LEQUAL allows sprites
    // to render over same-bucket tile geometry (tiles flush before sprites
    // in the DrawCmd list, writing depth; sprites at equal depth still pass).
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    int  bound_variation   = -1;
    bool flatpoly_uploaded = false;  // flat-poly VBO uploaded on first CMD_FLAT_POLYS

    for (const auto& cmd : m_draw_cmds)
    {
        if (cmd.type == DrawCmd::CMD_TILES)
        {
            if (cmd.variation != bound_variation)
            {
                GLuint atlas_tex = (m_atlas && m_atlas->IsInitialized())
                                   ? m_atlas->GetAtlasTexture(cmd.variation)
                                   : 0;
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, atlas_tex);
                bound_variation = cmd.variation;
            }
            // Always rebind the 1D palette at unit 1 — keeper-sprite and other
            // passes bind their own textures to unit 1 (GL_TEXTURE_2D), which
            // causes sampler1D u_palette to return black for subsequent tile draws.
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_1D, m_palette_tex);
            glDrawArrays(GL_TRIANGLES, cmd.vert_start, cmd.vert_count);
        }
        else if (cmd.type == DrawCmd::CMD_SHADOWS)
        {
            // Render a creature shadow: rasterise the silhouette into big_scratch,
            // upload as a 256×256 R8 texture, then draw the floor-projected quad
            // with a multiply-darken blend (GL_ZERO / GL_ONE_MINUS_SRC_ALPHA).
            const ShadowCmd& sc = m_shadow_cmds[cmd.shadow_idx];

            // CPU step: fill big_scratch with the sprite silhouette
            draw_keepsprite_unscaled_in_buffer(sc.anim_sprite, sc.angle,
                                               sc.current_frame, big_scratch);

            // Upload silhouette to the reusable R8 texture
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_shadow_silhouette_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256,
                            GL_RED, GL_UNSIGNED_BYTE, big_scratch);

            // Build 6 float vertices: two triangles covering the quad (0,1,2) + (0,2,3)
            // PolyPoint.X/Y = integer screen pixels; U/V = 16.16 → normalise to [0,1]
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
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(sv), sv);

            // Shadow shader + blend state
            glBindVertexArray(m_shadow_vao);
            glUseProgram(m_shadow_shader);
            glUniform2f(m_shadow_loc_viewport, (float)m_screen_w, (float)m_screen_h);
            glUniform1f(m_shadow_loc_darkness, sc.darkness);

            glDepthMask(GL_FALSE);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

            glDrawArrays(GL_TRIANGLES, 0, 6);

            // Restore tile rendering state
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glUseProgram(m_shader);
            glBindVertexArray(m_vao);
            // Rebind the atlas texture that was displaced by the silhouette
            bound_variation = -1;
        }
        else if (cmd.type == DrawCmd::CMD_SPRITES)
        {
            // 3D sprites have positions in viewport-relative pixel space
            // [0, m_screen_w) x [0, m_screen_h).  Keep the game viewport so
            // NDC [-1,1] maps to that region, and tell the sprite backend to
            // use viewport dimensions (not full-screen) for its NDC conversion.
            glBindVertexArray(0);
            glUseProgram(0);

            RenderPassSystem::GetInstance().SetScreenSize(m_screen_w, m_screen_h);

            // Assign this bucket's NDC depth to all sprites submitted during
            // the draw call, so hardware depth testing correctly occludes
            // sprites behind world geometry from closer buckets.
            const float sprite_z = 2.0f * (float)cmd.bucket_num / (float)(BUCKETS_COUNT - 1) - 1.0f;
            OpenGLSpriteBackend::SetCurrentBucketZ(sprite_z);

            // Store depth for world sprite submission through proper RenderPassSystem
            m_current_sprite_z = sprite_z;

            // By the time GPUFlushNow() runs, LbScreenLoadGraphicsWindow() has
            // already restored GraphicsWindowX/Y to 0 (full-screen bounds).
            // We must use the stored viewport origin (m_vp_x, m_vp_y) so that
            // CPU-blitted sprite fallbacks land at the correct offset inside the
            // staging buffer, not at the top-left corner over the sidebar.
            //
            // CRITICAL FIX: To prevent UI flickering, we must ensure that sprite
            // fallbacks only render within the 3D viewport area and don't overwrite
            // UI elements outside the viewport (like sidebar, messages).
            TbPixel* saved_wscreen = lbDisplay.WScreen;
            TbPixel* saved_gwptr   = lbDisplay.GraphicsWindowPtr;
            long saved_gw_x = lbDisplay.GraphicsWindowX;
            long saved_gw_y = lbDisplay.GraphicsWindowY;
            long saved_gw_w = lbDisplay.GraphicsWindowWidth;
            long saved_gw_h = lbDisplay.GraphicsWindowHeight;
            if (staging_buf != nullptr)
            {
                // Set up graphics window to point to the VIEWPORT AREA ONLY
                // This prevents sprite fallbacks from corrupting UI areas
                // 
                // CRITICAL FIX: Ensure sprite fallbacks are strictly contained within
                // the 3D viewport and cannot overwrite UI elements in the staging buffer.
                // Use a separate viewport-only buffer region to prevent pixel conflicts.
                lbDisplay.WScreen              = (TbPixel*)staging_buf;
                lbDisplay.GraphicsWindowX = m_vp_x;  // Restored: use viewport coordinates
                lbDisplay.GraphicsWindowY = m_vp_y;  // Restored: use viewport coordinates
                lbDisplay.GraphicsWindowWidth  = m_screen_w;
                lbDisplay.GraphicsWindowHeight = m_screen_h;

                // Calculate viewport offset more carefully to ensure proper bounds
                const long viewport_offset = (long)m_vp_x + (long)lbDisplay.GraphicsScreenWidth * (long)m_vp_y;

                // Validate viewport bounds to prevent buffer overruns
                if (viewport_offset >= 0 && 
                    (viewport_offset + m_screen_w + (long)lbDisplay.GraphicsScreenWidth * (m_screen_h - 1)) 
                    <= (long)lbDisplay.GraphicsScreenWidth * (long)lbDisplay.GraphicsScreenHeight)
                {
                    // Point to the viewport area within the staging buffer
                    lbDisplay.GraphicsWindowPtr = (TbPixel*)staging_buf + viewport_offset;
                }
                else
                {
                    // Fallback: point to start of staging buffer if bounds are invalid
                    lbDisplay.GraphicsWindowPtr = (TbPixel*)staging_buf;
                    WARNLOG("Viewport bounds validation failed: offset=%ld, viewport=%dx%d, screen=%dx%d",
                            viewport_offset, m_screen_w, m_screen_h, 
                            (int)lbDisplay.GraphicsScreenWidth, (int)lbDisplay.GraphicsScreenHeight);
                }
            }

            // Use proper RenderPassSystem approach - no more global hook needed
            setup_world_sprite_processing(cmd.bucket_num);

            draw_3d_sprites_for_bucket(cmd.bucket_num);

            RenderPass_FlushNow();

            // Restore default screen size for the render system

            RenderPassSystem::GetInstance().SetScreenSize(0, 0);

            lbDisplay.GraphicsWindowX      = saved_gw_x;
            lbDisplay.GraphicsWindowY      = saved_gw_y;
            lbDisplay.GraphicsWindowWidth  = saved_gw_w;
            lbDisplay.GraphicsWindowHeight = saved_gw_h;
            lbDisplay.WScreen              = saved_wscreen;
            lbDisplay.GraphicsWindowPtr    = saved_gwptr;

            // Restore the world shader and VAO for subsequent tile batches.
            glUseProgram(m_shader);
            glBindVertexArray(m_vao);
            // Force atlas rebind on the next CMD_TILES pass — keeper-sprite
            // rendering displaced GL_TEXTURE0 from the tile atlas.
            bound_variation = -1;
        }
        else if (cmd.type == DrawCmd::CMD_WORLDTEXT)
        {
            // Render world-space text with depth testing enabled
            const WorldTextCmd& wt = m_worldtext_cmds[cmd.worldtext_idx];

            if (m_text_renderer && wt.text)
            {
                // TODO: Project world position to screen coordinates
                // For now, render at a fixed test position
                float screen_x = 100.0f;  // Placeholder
                float screen_y = 100.0f;  // Placeholder

                // Set up depth testing for world text
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_FALSE);  // Don't write depth (allows overdraw)

                // Scale factor from world text scale  
                int units_per_px = (int)(wt.scale * 16.0f);

                // Render the text at the calculated position
                m_text_renderer->DrawTextResized((int)screen_x, (int)screen_y, 
                                                units_per_px, wt.text);

                // Restore state
                glDepthMask(GL_TRUE);

                // Rebind world renderer state for subsequent commands
                glUseProgram(m_shader);
                glBindVertexArray(m_vao);
                bound_variation = -1;
            }
        }
        else if (cmd.type == DrawCmd::CMD_FLAT_POLYS)
        {
            // Flat-colour polygons: all vertices already converted to screen-px + linear RGB.
            // Upload the entire flat-poly buffer once on first encounter, draw sub-range.
            if (!m_flatpoly_verts.empty())
            {
                if (!flatpoly_uploaded)
                {
                    glBindBuffer(GL_ARRAY_BUFFER, m_flatpoly_vbo);
                    glBufferData(GL_ARRAY_BUFFER,
                                 (GLsizeiptr)(m_flatpoly_verts.size() * sizeof(FlatPolyVertex)),
                                 m_flatpoly_verts.data(), GL_STREAM_DRAW);
                    flatpoly_uploaded = true;
                }
                glUseProgram(m_flatpoly_shader);
                glBindVertexArray(m_flatpoly_vao);
                glUniform2f(m_flatpoly_loc_viewport, (float)m_screen_w, (float)m_screen_h);
                glDrawArrays(GL_TRIANGLES, cmd.vert_start, cmd.vert_count);
                // Restore tile shader state for subsequent CMD_TILES.
                glUseProgram(m_shader);
                glBindVertexArray(m_vao);
                bound_variation = -1;
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, (int)MyScreenWidth, (int)MyScreenHeight);

    // Reset for next frame.
    m_draw_cmds.clear();
    m_shadow_cmds.clear();
    m_worldtext_cmds.clear();
    m_flatpoly_verts.clear();
    m_vert_count     = 0;
    m_cmd_vert_start = 0;
}

/******************************************************************************/

void GLWorldViewRenderer::FlushIsometricView()
{
    if (!m_initialized) {
        // Fall back to software if GL resources not ready
        if (m_sw_fallback) m_sw_fallback->FlushIsometricView();
        return;
    }

    // Walk the depth-sorted bucket list back-to-front (painter's algorithm).
    // For each bucket:
    //   Geometry types are accumulated in the VBO (batched across buckets for
    //   efficiency).  When a bucket contains 3D entity sprites (JontySprite /
    //   JontyISOSprite), the accumulated tile geometry is flushed to GL first,
    //   the sprite draw functions are called (which queue quads to the GPU
    //   sprite backend), then the sprite quads are immediately flushed.
    // Non-spatial elements (shadows, selector, status, text, room flags) are
    // drawn afterwards into the CPU staging buffer via draw_nonspatial_sprites().
    for (int bi = BUCKETS_COUNT - 1; bi > 0; bi--)
    {
        m_current_bucket = bi;
        const float z_ndc = 2.0f * (float)bi / (float)(BUCKETS_COUNT - 1) - 1.0f;
        bool bucket_has_3d_sprites = false;
        bool bucket_has_flat_polys = false;
        const int flatpoly_vert_start = (int)m_flatpoly_verts.size();

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
                                    &p->vertex_third);
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
                                    &p->vertex_third);
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
                    // Flush any pending tile geometry so it lands before this shadow.
                    gpu_flush();
                    ShadowCmd sc;
                    sc.verts[0]       = s->vertex_first;
                    sc.verts[1]       = s->vertex_second;
                    sc.verts[2]       = s->vertex_third;
                    sc.verts[3]       = s->vertex_fourth;
                    sc.anim_sprite    = s->anim_sprite;
                    sc.current_frame  = s->current_frame;
                    sc.angle          = (short)s->angle;
                    // darkness = 1 - shadow_factor; shadow_factor = color_value/32
                    // color_value is 16..31: closer light → lower value → darker shadow
                    sc.darkness = 1.0f - (float)s->color_value / 32.0f;
                    DrawCmd cmd;
                    cmd.type       = DrawCmd::CMD_SHADOWS;
                    cmd.shadow_idx = (int)m_shadow_cmds.size();
                    m_shadow_cmds.push_back(sc);
                    m_draw_cmds.push_back(cmd);
                    break;
                }

                // ── All other types go to draw_nonspatial_sprites() ──────────
                default:
                    break;
            }
            q = q->next;
        }

        // Commit accumulated tile geometry as a batch command, then record a
        // sprite command for this bucket.  Both will be replayed in GPUFlushNow()
        // after glClear(), preserving the painter's-algorithm depth order.
        if (bucket_has_3d_sprites)
        {
            gpu_flush();

            DrawCmd cmd;
            cmd.type       = DrawCmd::CMD_SPRITES;
            cmd.bucket_num = bi;
            m_draw_cmds.push_back(cmd);
        }

        // Flat-colour polygons: GPU triangle draw via flat-poly shader.
        if (bucket_has_flat_polys)
        {
            gpu_flush();

            DrawCmd cmd;
            cmd.type       = DrawCmd::CMD_FLAT_POLYS;
            cmd.vert_start = flatpoly_vert_start;
            cmd.vert_count = (int)m_flatpoly_verts.size() - flatpoly_vert_start;
            m_draw_cmds.push_back(cmd);
        }
    }

    // Draw non-spatial elements (slab selector, status icons, floating gold text,
    // room flags) using GPU-accelerated UI renderer instead of CPU staging buffer.
    // Creature shadows are handled above via CMD_SHADOWS and rendered GPU-side.
    draw_nonspatial_sprites_gpu();
}

void GLWorldViewRenderer::FlushFrontView(struct Camera* cam)
{
    // Front-view uses the same bucket mechanism; delegate to software for now.
    if (m_sw_fallback)
        m_sw_fallback->FlushFrontView(cam);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
