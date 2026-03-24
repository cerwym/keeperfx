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
#include "bflib_render.h"      // PolyPoint, render_fade_tables
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG / WARNLOG
#include "engine_textures.h"   // TEXTURE_VARIATIONS_COUNT

#include <glad/glad.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/
// render_fade_tables declared in bflib_render.h (256×256 lighting LUT)
// lbPaletteColors / lbDisplay.Palette declared in bflib_video.h

/******************************************************************************/
// Shader file paths (relative to working directory — game data root)
static const char* k_vert_path = "src/renderer/opengl/shaders/world_vert.glsl";
static const char* k_frag_path = "src/renderer/opengl/shaders/world_frag.glsl";

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

bool GLWorldViewRenderer::load_shader_source(const char* path, char** out_src)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        ERRORLOG("GLWorldViewRenderer: cannot open shader '%s'", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    *out_src = (char*)malloc((size_t)sz + 1);
    if (!*out_src) { fclose(f); return false; }
    fread(*out_src, 1, (size_t)sz, f);
    (*out_src)[sz] = '\0';
    fclose(f);
    return true;
}

bool GLWorldViewRenderer::compile_world_shaders()
{
    char* vert_src = nullptr;
    char* frag_src = nullptr;
    bool ok = false;

    if (!load_shader_source(k_vert_path, &vert_src)) goto cleanup;
    if (!load_shader_source(k_frag_path, &frag_src)) goto cleanup;

    {
        GLuint vert = compile_shader_src(GL_VERTEX_SHADER,   vert_src, k_vert_path);
        GLuint frag = compile_shader_src(GL_FRAGMENT_SHADER, frag_src, k_frag_path);
        if (!vert || !frag)
        {
            if (vert) glDeleteShader(vert);
            if (frag) glDeleteShader(frag);
            goto cleanup;
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
            goto cleanup;
        }
        ok = true;
    }

cleanup:
    free(vert_src);
    free(frag_src);
    return ok;
}

/******************************************************************************/

void GLWorldViewRenderer::BeginWorldPass(unsigned char* framebuf, int pitch,
                                          int w, int h)
{
    m_screen_w   = w;
    m_screen_h   = h;
    m_vert_count = 0;

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

void GLWorldViewRenderer::gpu_flush()
{
    if (m_vert_count == 0 || !m_initialized)
        return;

    // Use variation 0 for now; full player-colour variation selection is TODO
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

/******************************************************************************/

void GLWorldViewRenderer::FlushIsometricView()
{
    if (!m_initialized) {
        // Fall back to software if GL resources not ready
        if (m_sw_fallback) m_sw_fallback->FlushIsometricView();
        return;
    }

    // Walk the depth-sorted bucket list (front-to-back = bucket 0 first)
    for (int bi = 0; bi < BUCKETS_COUNT; bi++)
    {
        struct BasicQ* q = buckets[bi];
        while (q != nullptr)
        {
            switch (q->kind)
            {
                // ── Textured triangles with PolyPoint vertices ──────────────
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

                // ── Deferred: compact-format variants ───────────────────────
                // QK_TrigMode2/3/6, QK_PolyMode0/4/5 use unsigned short x/y
                // and unsigned char UV — different conversion path needed.
                // QK_RotableSprite, QK_JontySprite, QK_JontyISOSprite,
                // QK_CreatureShadow, QK_SlabSelector, QK_CreatureStatus,
                // QK_TextureQuad, QK_FloatingGoldText, QK_RoomFlag* —
                // sprite/UI types, not yet ported to GPU path.
                default:
                    break;
            }
            q = q->next;
        }
    }

    // Issue the accumulated triangles in one draw call
    gpu_flush();
}

void GLWorldViewRenderer::FlushFrontView(struct Camera* cam)
{
    // Front-view uses the same bucket mechanism; delegate to software for now.
    if (m_sw_fallback)
        m_sw_fallback->FlushFrontView(cam);
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
