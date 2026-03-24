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
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/ITileAtlas.h"

#include "engine_buckets.h"   // QKinds enum, BasicQ, BucketKind* structs, buckets[]
#include "engine_render.h"    // display_drawlist_sprites_only()
#include "bflib_render.h"      // PolyPoint, render_fade_tables
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG / WARNLOG

#include <glad/glad.h>
#include <cstdlib>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

static const char* k_world_vert_src = R"glsl(
#version 100
attribute vec2  a_pos;
attribute vec2  a_uv;
attribute float a_shade;
varying vec2  v_uv;
varying float v_shade;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv        = a_uv;
    v_shade     = a_shade;
}
)glsl";

static const char* k_world_frag_src = R"glsl(
#version 100
precision mediump float;
varying vec2  v_uv;
varying float v_shade;
uniform sampler2D u_tile_atlas;
uniform sampler2D u_fade_table;
uniform sampler2D u_palette;
void main()
{
    float raw_idx    = texture2D(u_tile_atlas, v_uv).r;
    float shaded_idx = texture2D(u_fade_table, vec2(raw_idx, v_shade)).r;
    gl_FragColor     = texture2D(u_palette, vec2(shaded_idx, 0.5));
}
)glsl";

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

GLWorldViewRenderer::GLWorldViewRenderer(ITileAtlas* atlas,
                                         GLuint      fade_tex,
                                         GLuint      palette_tex)
    : m_atlas(atlas)
    , m_fade_tex(fade_tex)
    , m_palette_tex(palette_tex)
{
    m_sw_fallback = new SoftwareWorldViewRenderer();
}

GLWorldViewRenderer::~GLWorldViewRenderer()
{
    free_gl_resources();
    delete m_sw_fallback;
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

    // layout(location=0) vec2 a_pos  — x,y at byte offset 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)0);
    glEnableVertexAttribArray(0);
    // layout(location=1) vec2 a_uv   — u,v at byte offset 8 (after x,y)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // layout(location=2) float a_shade — shade at byte offset 16 (after x,y,u,v)
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(WorldVertex),
                          (void*)(4 * sizeof(float)));
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

    // Cache uniform locations
    glUseProgram(m_shader);
    m_loc_tile_atlas = glGetUniformLocation(m_shader, "u_tile_atlas");
    m_loc_fade_table = glGetUniformLocation(m_shader, "u_fade_table");
    m_loc_palette    = glGetUniformLocation(m_shader, "u_palette");
    glUniform1i(m_loc_tile_atlas, 0);   // GL_TEXTURE0
    glUniform1i(m_loc_fade_table, 1);   // GL_TEXTURE1
    glUniform1i(m_loc_palette,    2);   // GL_TEXTURE2
    glUseProgram(0);

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
    m_initialized = false;
}

bool GLWorldViewRenderer::compile_world_shaders()
{
    GLuint vert = compile_shader_src(GL_VERTEX_SHADER,   k_world_vert_src, "world_vert");
    GLuint frag = compile_shader_src(GL_FRAGMENT_SHADER, k_world_frag_src, "world_frag");
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

/******************************************************************************/

void GLWorldViewRenderer::BeginWorldPass(unsigned char* framebuf, int pitch,
                                          int w, int h)
{
    m_screen_w   = w;
    m_screen_h   = h;
    m_framebuf   = framebuf;
    m_pitch      = pitch;
    m_vert_count = 0;

    // Zero the viewport area in the CPU staging buffer so palette index 0
    // acts as transparent in the compositing blit, letting GPU-rendered tiles
    // show through.
    if (framebuf)
    {
        for (int row = 0; row < h; row++)
            memset(framebuf + (long)row * pitch, 0, (size_t)w);
    }

    // Lazy initialise GL resources on first use (GL context must be current)
    if (!m_initialized)
        init_gl_resources();

    // Software fallback always receives the pass too (it sets vec globals)
    if (m_sw_fallback)
        m_sw_fallback->BeginWorldPass(framebuf, pitch, w, h);
}

/******************************************************************************/

bool GLWorldViewRenderer::append_triangle(const struct PolyPoint* p0,
                                           const struct PolyPoint* p1,
                                           const struct PolyPoint* p2)
{
    if (m_vert_count + 3 > k_max_verts)
    {
        // Mid-frame flush to avoid overflow
        gpu_flush();
    }

    WorldVertex* v = &m_verts[m_vert_count];
    POLYPOINT_TO_WORLDVERTEX(&v[0], p0, m_screen_w, m_screen_h);
    POLYPOINT_TO_WORLDVERTEX(&v[1], p1, m_screen_w, m_screen_h);
    POLYPOINT_TO_WORLDVERTEX(&v[2], p2, m_screen_w, m_screen_h);
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

    WorldVertex* v = &m_verts[m_vert_count];
    COMPACT_UV_TO_WORLDVERTEX(&v[0], sx0, sy0, u0, v0, shade0, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[1], sx1, sy1, u1, v1, shade1, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&v[2], sx2, sy2, u2, v2, shade2, m_screen_w, m_screen_h);
    m_vert_count += 3;
    return true;
}

void GLWorldViewRenderer::gpu_flush()
{
    if (m_vert_count == 0 || !m_initialized)
        return;

    // Variation 0 matches the software path (block_ptrs[] uses raw tile_id)
    const int variation = 0;
    GLuint atlas_tex = (m_atlas && m_atlas->IsInitialized())
                       ? m_atlas->GetAtlasTexture(variation)
                       : 0;

    glUseProgram(m_shader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_fade_tex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(m_vert_count * sizeof(WorldVertex)),
                    m_verts);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_vert_count);
    glBindVertexArray(0);
    glUseProgram(0);

    m_vert_count = 0;
}

void GLWorldViewRenderer::GPUFlushNow()
{
    gpu_flush();
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
    // Geometry types are emitted to the VBO; sprite/UI types write directly to
    // the CPU staging buffer (zeroed in BeginWorldPass) for the overlay blit.
    for (int bi = BUCKETS_COUNT - 1; bi > 0; bi--)
    {
        struct BasicQ* q = buckets[bi];
        while (q != nullptr)
        {
            switch (q->kind)
            {
                // ── Full PolyPoint (fixed-point 16:16) geometry ─────────────
                case QK_PolygonStandard:
                {
                    auto* p = (struct BucketKindPolygonStandard*)q;
                    append_triangle(&p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third);
                    break;
                }
                case QK_PolygonSimple:
                {
                    auto* p = (struct BucketKindPolygonSimple*)q;
                    append_triangle(&p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third);
                    break;
                }
                case QK_PolygonNearFP:
                {
                    auto* p = (struct BucketKindPolygonNearFP*)q;
                    append_triangle(&p->vertex_first,
                                    &p->vertex_second,
                                    &p->vertex_third);
                    break;
                }
                case QK_BasicPolygon:
                {
                    auto* p = (struct BucketKindBasicUnk10*)q;
                    append_triangle(&p->vertex_first,
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

                // ── Flat-colour types (no UV, different shader needed) ───────
                // QK_PolyMode0 / QK_PolyMode4: silently skipped for now
                case QK_PolyMode0:
                case QK_PolyMode4:
                    break;

                // ── Sprite / UI types: handled by CPU software overlay ───────
                // display_drawlist_sprites_only() is called below; these bucket
                // kinds write to the zeroed CPU staging buffer, which is then
                // alpha-blended over the GPU geometry in RendererOpenGL::EndFrame.
                default:
                    break;
            }
            q = q->next;
        }
    }

    // Draw sprites and UI to the CPU staging buffer (zeroed in BeginWorldPass).
    // GPUFlushNow() is called by RendererOpenGL::EndFrame() BEFORE the blit, so
    // the GPU geometry is already below the CPU overlay in the final frame.
    display_drawlist_sprites_only();
}

void GLWorldViewRenderer::FlushFrontView(struct Camera* cam)
{
    // Front-view uses the same bucket mechanism; delegate to software for now.
    if (m_sw_fallback)
        m_sw_fallback->FlushFrontView(cam);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
