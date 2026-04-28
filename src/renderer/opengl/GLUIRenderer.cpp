/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLUIRenderer.cpp
 *     OpenGL hardware-accelerated UI element renderer implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLUIRenderer.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLShaders.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include "renderer/opengl/GLFontAtlas.h"
#include "renderer/RendererManager.h"
#include "bflib_basics.h"
#include "bflib_video.h"       // lbDisplay.DrawFlags
#include "globals.h"

#include <glad/glad.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "renderer/VecMath.h"
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

/******************************************************************************/

GLUIRenderer::GLUIRenderer()
    : m_prog_sprite(0)
    , m_prog_font(0)
    , m_prog_solid(0)
    , m_prog_remap(0)
    , m_prog_fbo(0)
    , m_loc_screen_sprite(-1)
    , m_loc_screen_font(-1)
    , m_loc_screen_solid(-1)
    , m_loc_screen_remap(-1)
    , m_loc_remap_row(-1)
    , m_loc_screen_fbo(-1)
    , m_loc_fbo_clip_rect(-1)
    , m_loc_fbo_clip_radius(-1)
    , m_loc_fbo_clip_scrh(-1)
    , m_vao(0)
    , m_vbo(0)
    , m_uniform_mvp(0)
    , m_uniform_texture(0)
    , m_screen_width(0)
    , m_screen_height(0)
    , m_sprite_atlas(nullptr)
    , m_font_atlas(nullptr)
    , m_palette_texture(0)
    , m_palette_texture_target(GL_TEXTURE_2D)
    , m_fade_texture(0)
    , m_minimap_cpu_buf(nullptr)
    , m_minimap_cpu_size(0)
    , m_minimap_texture(0)
    , m_minimap_tex_size(0)
    , m_minimap_x(0)
    , m_minimap_y(0)
    , m_minimap_size(0)
    , m_minimap_pending(false)
{
    m_ui_quads.reserve(512);
    m_ui_lines.reserve(256);
    m_vertices.reserve(3072);
    m_fbo_quads.reserve(4);
}

GLUIRenderer::~GLUIRenderer()
{
    Shutdown();
}

bool GLUIRenderer::Init()
{
    // Create the three independent shader programs.
    if (!CreateShaders())
    {
        ERRORLOG("GLUIRenderer: Failed to create shaders");
        return false;
    }

    // Create vertex arrays and buffers (shared across all three programs,
    // same VAO layout for UI_VERTEX_SHADER).
    CreateVertexArrays();
    return true;
}

void GLUIRenderer::Shutdown()
{
    delete[] m_minimap_cpu_buf;
    m_minimap_cpu_buf  = nullptr;
    m_minimap_cpu_size = 0;
    if (m_minimap_texture) {
        glDeleteTextures(1, &m_minimap_texture);
        m_minimap_texture = 0;
        m_minimap_tex_size = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_prog_sprite) { glDeleteProgram(m_prog_sprite); m_prog_sprite = 0; }
    if (m_prog_sprite_colored) { glDeleteProgram(m_prog_sprite_colored); m_prog_sprite_colored = 0; }
    if (m_prog_font)   { glDeleteProgram(m_prog_font);   m_prog_font   = 0; }
    if (m_prog_solid)  { glDeleteProgram(m_prog_solid);  m_prog_solid  = 0; }
    if (m_prog_remap)  { glDeleteProgram(m_prog_remap);  m_prog_remap  = 0; }
    if (m_prog_fbo)    { glDeleteProgram(m_prog_fbo);    m_prog_fbo    = 0; }
    if (m_slab_texture) { glDeleteTextures(1, &m_slab_texture); m_slab_texture = 0; }
}

void GLUIRenderer::SetScreenDimensions(int width, int height)
{
    m_screen_width = width;
    m_screen_height = height;
}

bool GLUIRenderer::SetSpriteAtlas(GLSpriteAtlas* atlas)
{
    m_sprite_atlas = atlas;
    return true;
}

bool GLUIRenderer::SetFontAtlas(GLFontAtlas* atlas)
{
    m_font_atlas = atlas;
    return true;
}

bool GLUIRenderer::SetPaletteTexture(GLuint palette_texture_id, GLenum target)
{
    m_palette_texture        = palette_texture_id;
    m_palette_texture_target = target;
    return true;
}

void GLUIRenderer::SetFadeTexture(GLuint tex)
{
    m_fade_texture = tex;
}

void GLUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth)
{
    // Convert color index to RGB
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    // TODO: Proper palette color lookup for line color
    
    SubmitLine((float)x1, (float)y1, (float)x2, (float)y2, r, g, b, a, z_depth, 2.0f);
}

extern "C" unsigned char EngineSpriteDrawUsingAlpha;

// Returns the alpha that should be applied to a submitted UI element based
// on the current lbDisplay.DrawFlags transparency flags.
// The ghost table (pixmap.ghost) computes (src*1/3 + dst*2/3) for both
// TRANSPAR4 and TRANSPAR8 modes, so src alpha = 1/3 ≈ 0.333f.
static inline float UIAlphaFromDrawFlags()
{
    if (lbDisplay.DrawFlags & (Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8)) return 0.333f;
    return 1.0f;
}

void GLUIRenderer::SubmitPanelSprite(int32_t x, int32_t y, int units_per_px, SpriteHandle spr, bool flip_horiz)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas) return;
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) return;
    // Reproduce the LbSpriteDrawResized rounding: (w * upp + 8) / 16
    float w = (float)((uv.pixel_w * units_per_px + 8) / 16);
    float h = (float)((uv.pixel_h * units_per_px + 8) / 16);
    float u0 = flip_horiz ? uv.u1 : uv.u0;
    float u1 = flip_horiz ? uv.u0 : uv.u1;
    float a = UIAlphaFromDrawFlags();
    SubmitQuad((float)x, (float)y, w, h,
               u0, uv.v0, u1, uv.v1,
               1.0f, 1.0f, 1.0f, a, 0.5f, 0.0f);
}

void GLUIRenderer::SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, int remap_row)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas || !m_fade_texture) {
        // Fallback: no fade texture or atlas — let base class handle it via CPU path
        IUIRenderer::SubmitPanelSpriteRemap(x, y, units_per_px, spr, remap_row);
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) return;
    float w = (float)((uv.pixel_w * units_per_px + 8) / 16);
    float h = (float)((uv.pixel_h * units_per_px + 8) / 16);
    UIQuad q;
    q.x0 = (float)x;  q.y0 = (float)y;
    q.x1 = (float)x + w;  q.y1 = (float)y + h;
    q.u0 = uv.u0;  q.v0 = uv.v0;
    q.u1 = uv.u1;  q.v1 = uv.v1;
    q.r = 1.0f;  q.g = 1.0f;  q.b = 1.0f;  q.a = UIAlphaFromDrawFlags();
    q.z = 0.5f;
    q.mode = 30.0f;
    q.texture_id = 0;
    q.layer = static_cast<uint8_t>(m_current_layer);
    q.remap_row = remap_row;
    m_ui_quads.push_back(q);
}

void GLUIRenderer::SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                            SpriteHandle spr, uint8_t color_idx)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas) return;
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) return;
    float w = (float)((uv.pixel_w * units_per_px + 8) / 16);
    float h = (float)((uv.pixel_h * units_per_px + 8) / 16);
    float r = lbPalette[color_idx * 3 + 0] / 63.0f;
    float g = lbPalette[color_idx * 3 + 1] / 63.0f;
    float b = lbPalette[color_idx * 3 + 2] / 63.0f;
    // mode=20.0: atlas-as-mask, flat vertex colour (Pass 5 in FlushQuads)
    SubmitQuad((float)x, (float)y, w, h,
               uv.u0, uv.v0, uv.u1, uv.v1,
               r, g, b, UIAlphaFromDrawFlags(), 0.5f, 20.0f);
}

void GLUIRenderer::SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h, SpriteHandle spr)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas) return;
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) return;
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               uv.u0, uv.v0, uv.u1, uv.v1,
               1.0f, 1.0f, 1.0f, UIAlphaFromDrawFlags(), 0.5f, 0.0f);
}

void GLUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx)
{
    float r = lbPalette[color_idx * 3 + 0] / 63.0f;
    float g = lbPalette[color_idx * 3 + 1] / 63.0f;
    float b = lbPalette[color_idx * 3 + 2] / 63.0f;
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f,
               r, g, b, 1.0f, 0.5f, 3.0f);
}

void GLUIRenderer::SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha)
{
    float r = lbPalette[color_idx * 3 + 0] / 63.0f;
    float g = lbPalette[color_idx * 3 + 1] / 63.0f;
    float b = lbPalette[color_idx * 3 + 2] / 63.0f;
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f,
               r, g, b, alpha, 0.5f, 3.0f);
}

void GLUIRenderer::UpdateSlabTexture(const uint8_t* data, int dim)
{
    if (!data || dim <= 0) return;
    if (m_slab_texture == 0) glGenTextures(1, &m_slab_texture);
    glBindTexture(GL_TEXTURE_2D, m_slab_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, dim, dim, 0, GL_RED, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_slab_dim = dim;
}

bool GLUIRenderer::SubmitSlabBackground(int x, int y, int w, int h)
{
    if (!m_slab_texture || m_slab_dim <= 0) return false;
    float u1 = (float)w / (float)m_slab_dim;
    float v1 = (float)h / (float)m_slab_dim;
    // Always submit on layer 0 (back, rendered before the staging-buffer blit)
    int saved_layer = m_current_layer;
    m_current_layer = 0;
    // Opaque black backing quad — blocks world geometry from bleeding through
    // any transparent pixels in the slab tile texture.  Submitted first so
    // the tiled slab renders on top via painter's order.
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f,
               0.0f, 0.0f, 0.0f, 1.0f, 0.48f, 3.0f);
    // Tiled slab texture on top
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, u1, v1,
               1.0f, 1.0f, 1.0f, 1.0f, 0.49f, 10.0f);
    m_current_layer = saved_layer;
    return true;
}

uint8_t* GLUIRenderer::AcquireMinimapBuffer(int size)
{
    if (size <= 0) return nullptr;

    // Grow the CPU buffer if the minimap size changed
    if (size != m_minimap_cpu_size)
    {
        delete[] m_minimap_cpu_buf;
        m_minimap_cpu_buf  = new uint8_t[size * size];
        m_minimap_cpu_size = size;
    }
    memset(m_minimap_cpu_buf, 0, (size_t)size * size);
    return m_minimap_cpu_buf;
}

void GLUIRenderer::SubmitMinimap(int screen_x, int screen_y, int size)
{
    if (size <= 0 || !m_minimap_cpu_buf) return;

    // Create or re-create the GL texture when size changes
    if (size != m_minimap_tex_size)
    {
        if (m_minimap_texture)
            glDeleteTextures(1, &m_minimap_texture);
        glGenTextures(1, &m_minimap_texture);
        glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        m_minimap_tex_size = size;
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RED, GL_UNSIGNED_BYTE, m_minimap_cpu_buf);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // restore default
    glBindTexture(GL_TEXTURE_2D, 0);

    m_minimap_x       = screen_x;
    m_minimap_y       = screen_y;
    m_minimap_size    = size;
    m_minimap_pending = true;
}

void GLUIRenderer::SetLayer(int layer)
{
    m_current_layer = layer;
}

void GLUIRenderer::DrawBack()
{
    KFX_ZONE("UIRenderer::DrawBack");
    KFX_GPU_ZONE("UIPass::Back");
    KFX_GL_SCOPE(back_grp, "UIPass/Back");
    // Render only layer-0 (back) quads — the sidebar background panels that must land
    // beneath the CPU staging-buffer blit.  No hand sprites, minimap, or lines here.
    if (m_ui_quads.empty()) return;

    { // Diagnostic: detect anomalous back-layer drops
        int back_quads = 0;
        for (auto& q : m_ui_quads) if (q.layer == 0) back_quads++;
        static int s_prev_back = 0;
        static int s_back_frame = 0;
        ++s_back_frame;
        bool anomaly = (s_prev_back >= 4 && back_quads < s_prev_back / 2);
        if (anomaly || (s_back_frame % 300) == 0)
            SYNCLOG("FLICKER-DIAG-BACK[%d]: back q=%d(prev %d)",
                    s_back_frame, back_quads, s_prev_back);
        s_prev_back = back_quads;
    }

    if (MyScreenWidth > 0 && MyScreenHeight > 0) {
        m_screen_width  = MyScreenWidth;
        m_screen_height = MyScreenHeight;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    FlushQuads(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

// Draw all queued FBO/PiP composite quads.  Called at the start of FlushFront()
// so that decorative frame sprites (layer 1) render on top.
static void FlushFBOQuads_impl(const std::vector<FBOQuad>& quads, GLuint prog, GLint loc_screen, GLuint vao, GLuint vbo,
                               int screen_w, int screen_h,
                               GLint loc_clip_rect, GLint loc_clip_radius, GLint loc_clip_scrh) {
    if (quads.empty() || !prog)
        return;

    // Scissor test must be off: a stale scissor rect from a previous pass (e.g. a UI
    // clipping region) would silently discard parts of the FBO quad.
    GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(prog);
    glUniform2f(loc_screen, (float)screen_w, (float)screen_h);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    for (const FBOQuad& q : quads) {
        glBindTexture(GL_TEXTURE_2D, q.tex_id);

        // Set rounded-rect clip uniforms per quad.
        glUniform1f(loc_clip_radius, q.clip_radius);
        glUniform1f(loc_clip_scrh, (float)screen_h);
        if (q.clip_radius >= 0.0f)
        {
            float cr[4] = { q.x0, q.y0, q.x1, q.y1 };
            glUniform4fv(loc_clip_rect, 1, cr);
        }

        // Build two-triangle quad: screen-pixel coords, V flipped.
        // GL FBOs store Y=0 at the bottom, so the world-render puts game-screen
        // y=0 (far tiles) at texture V=1.  Flip V so the top of the PiP box
        // shows the far end of the view rather than the near end.
        // GLUIVertex: x, y, u, v, r, g, b, a, z, mode
        GLUIVertex verts[6] = {
            {q.x0, q.y0, 0.f, 1.f, 1, 1, 1, 1, 0.f, 0.f}, {q.x1, q.y0, 1.f, 1.f, 1, 1, 1, 1, 0.f, 0.f},
            {q.x1, q.y1, 1.f, 0.f, 1, 1, 1, 1, 0.f, 0.f}, {q.x0, q.y0, 0.f, 1.f, 1, 1, 1, 1, 0.f, 0.f},
            {q.x1, q.y1, 1.f, 0.f, 1, 1, 1, 1, 0.f, 0.f}, {q.x0, q.y1, 0.f, 0.f, 1, 1, 1, 1, 0.f, 0.f},
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    if (scissor_was_enabled)
        glEnable(GL_SCISSOR_TEST);
}

void GLUIRenderer::DrawFront()
{
    KFX_ZONE("UIRenderer::DrawFront");
    KFX_GPU_ZONE("UIPass::Front");
    KFX_GL_SCOPE(front_grp, "UIPass/Front");
    if (m_ui_quads.empty() && m_ui_lines.empty() && !m_minimap_pending && m_fbo_quads.empty())
    {
        static int s_empty_count = 0;
        if (++s_empty_count <= 100)
            SYNCLOG("FLICKER-DIAG: FlushFront EMPTY (frame %d)", s_empty_count);
        return;
    }

    { // Diagnostic: detect anomalous frame-to-frame quad count changes
        int front_quads = 0;
        for (auto& q : m_ui_quads) if (q.layer == 1) front_quads++;
        int front_lines = 0;
        for (auto& l : m_ui_lines) if (l.layer == 1) front_lines++;
        static int s_prev_quads  = 0;
        static int s_prev_lines  = 0;
        static int s_frame_num   = 0;
        ++s_frame_num;
        // Log when quad count drops by ≥50% or to zero (from ≥4).
        bool anomaly = (s_prev_quads >= 4 && front_quads < s_prev_quads / 2);
        if (anomaly || (s_frame_num % 300) == 0)
            SYNCLOG("FLICKER-DIAG[%d]: front q=%d(prev %d) lines=%d(prev %d) minimap=%d",
                    s_frame_num, front_quads, s_prev_quads,
                    front_lines, s_prev_lines, (int)m_minimap_pending);
        s_prev_quads = front_quads;
        s_prev_lines = front_lines;
    }

    if (MyScreenWidth > 0 && MyScreenHeight > 0) {
        m_screen_width  = MyScreenWidth;
        m_screen_height = MyScreenHeight;
    }

    // Guarantee full-screen viewport.  The PiP path leaves the viewport at
    // pip_w×pip_h after FlushPiPSprites(); without this reset every draw call
    // below would be clipped to the tiny pip-sized scissor region.
    glViewport(0, 0, (int)MyScreenWidth, (int)MyScreenHeight);

    // ── FLICKER-DIAG: check for stale scissor or wrong FBO ──
    {
        GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLint draw_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
        if (scissor || draw_fbo != 0) {
            SYNCLOG("FLICKER-DIAG: BAD GL STATE entering DrawFront: scissor=%d fbo=%d",
                    (int)scissor, draw_fbo);
            if (scissor) glDisable(GL_SCISSOR_TEST);
            if (draw_fbo != 0) glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // FBO/PiP composite quads drawn first so frame-corner sprites sit on top.
    FlushFBOQuads_impl(m_fbo_quads, m_prog_fbo, m_loc_screen_fbo,
                       m_vao, m_vbo, m_screen_width, m_screen_height,
                       m_loc_fbo_clip_rect, m_loc_fbo_clip_radius, m_loc_fbo_clip_scrh);
    m_fbo_quads.clear();
    // Re-enable blend for atlas sprites.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Flush all remaining (layer-1 / front) quads.
    FlushQuads(1);

    // Minimap: palette-indexed R8 texture — front layer.
    if (m_minimap_pending && m_minimap_texture)
    {
        float mx = (float)m_minimap_x;
        float my = (float)m_minimap_y;
        float ms = (float)m_minimap_size;

        glUseProgram(m_prog_sprite);
        glUniform2f(m_loc_screen_sprite, (float)m_screen_width, (float)m_screen_height);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
        if (m_palette_texture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(m_palette_texture_target, m_palette_texture);
        }

        GLUIVertex verts[6] = {
            {mx,      my,      0.0f, 0.0f, 1,1,1,1, 0.49f, 0.0f},
            {mx,      my + ms, 0.0f, 1.0f, 1,1,1,1, 0.49f, 0.0f},
            {mx + ms, my,      1.0f, 0.0f, 1,1,1,1, 0.49f, 0.0f},
            {mx + ms, my,      1.0f, 0.0f, 1,1,1,1, 0.49f, 0.0f},
            {mx,      my + ms, 0.0f, 1.0f, 1,1,1,1, 0.49f, 0.0f},
            {mx + ms, my + ms, 1.0f, 1.0f, 1,1,1,1, 0.49f, 0.0f},
        };
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        m_minimap_pending = false;
    }

    // Flush slab-selector lines and other line geometry (front layer).
    FlushLines(1);

    // Flush world-depth-tagged elements (layer 2) — these are non-spatial sprites
    // (status flowers, room flags, slab selectors, floating numbers) submitted via
    // SetWorldDepth()/ClearWorldDepth() during draw_nonspatial_sprites_gpu().  They
    // carry an NDC z matching their bucket depth, so the existing tile depth buffer
    // will correctly occlude anything behind a wall.
    {
        bool has_layer2_quads = false;
        for (const auto& q : m_ui_quads) if (q.layer == 2) { has_layer2_quads = true; break; }
        bool has_layer2_lines = false;
        for (const auto& l : m_ui_lines) if (l.layer == 2) { has_layer2_lines = true; break; }
        if (has_layer2_quads || has_layer2_lines)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);   // depth writes ON — shaders discard transparent
                                    // fragments, so only opaque pixels write depth.
                                    // The GPU handles occlusion natively; no painter's
                                    // algorithm or mode-ordered passes needed.
            FlushQuads(2);
            FlushLines(2);
            glDisable(GL_DEPTH_TEST);
        }
    }

    // Layer 3: top-overlay — cursor-driven affordances (slab selector) drawn dead-last,
    // depth test OFF, so they are never obscured by any world or UI element.
    FlushQuads(3);
    FlushLines(3);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLUIRenderer::BeginPiPSprites()
{
    m_pip_quad_watermark  = (int)m_ui_quads.size();
    m_pip_line_watermark  = (int)m_ui_lines.size();
    m_pip_capture_active  = true;
}

void GLUIRenderer::DrawPiPSprites(int pip_w, int pip_h)
{
    if (!m_pip_capture_active)
        return;
    m_pip_capture_active = false;

    const int nq = (int)m_ui_quads.size()   - m_pip_quad_watermark;
    const int nl = (int)m_ui_lines.size()   - m_pip_line_watermark;

    if (nq > 0 || nl > 0)
    {
        // FBO is already bound by caller.  Use pip dimensions for NDC conversion.
        glViewport(0, 0, pip_w, pip_h);
        const int saved_w = m_screen_width;
        const int saved_h = m_screen_height;
        m_screen_width  = pip_w;
        m_screen_height = pip_h;

        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        // Temporarily move pip-tail entries to the front of each vector so that
        // FlushQuads/FlushLines (which operate on the whole vector
        // filtered by layer) only see pip-sourced entries.
        // We swap them into temporary vectors, flush, then restore the originals.
        std::vector<UIQuad>      pip_quads(m_ui_quads.begin()   + m_pip_quad_watermark,  m_ui_quads.end());
        std::vector<UILine>      pip_lines(m_ui_lines.begin()   + m_pip_line_watermark,  m_ui_lines.end());

        // Erase pip tail from the main queues now — corner sprites at [0..watermark) remain.
        m_ui_quads.erase(  m_ui_quads.begin()   + m_pip_quad_watermark,  m_ui_quads.end());
        m_ui_lines.erase(  m_ui_lines.begin()   + m_pip_line_watermark,  m_ui_lines.end());

        // Swap pip entries in as the active queues for the flush calls.
        m_ui_quads.swap(pip_quads);
        m_ui_lines.swap(pip_lines);

        // Layer-2: creature status / gold text — depth-tested against FBO geometry.
        {
            bool has2q = false, has2l = false;
            for (const auto& q : m_ui_quads)    if (q.layer == 2) { has2q = true; break; }
            for (const auto& l : m_ui_lines)    if (l.layer == 2) { has2l = true; break; }
            if (has2q || has2l)
            {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                if (has2q) FlushQuads(2);
                if (has2l) FlushLines(2);
                glDisable(GL_DEPTH_TEST);
            }
        }
        // Layer-1: room flags — always on top inside the zoom box.
        {
            bool has1q = false, has1l = false;
            for (const auto& q : m_ui_quads)    if (q.layer == 1) { has1q = true; break; }
            for (const auto& l : m_ui_lines)    if (l.layer == 1) { has1l = true; break; }
            if (has1q || has1l)
            {
                glDisable(GL_DEPTH_TEST);
                if (has1q) FlushQuads(1);
                if (has1l) FlushLines(1);
            }
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glUseProgram(0);
        m_screen_width  = saved_w;
        m_screen_height = saved_h;

        // Discard any remaining pip entries (layer-3 top-overlay etc.).
        m_ui_quads.clear();
        m_ui_lines.clear();

        // Restore the saved corner-frame entries as the active queues.
        m_ui_quads.swap(pip_quads);
        m_ui_lines.swap(pip_lines);
    }
    else
    {
        // No pip-sourced sprites — nothing to render into the FBO.
        // The queues already contain only pre-PiP entries; nothing to restore.
    }
}

void GLUIRenderer::DrawCursorSprites()
{
    // Draw atlas-quad sprites submitted since the last DrawFront().
    // Called by GLCursorLayer::Draw() for the OS pointer sprite.
    if (m_ui_quads.empty()) return;
    if (MyScreenWidth > 0 && MyScreenHeight > 0) {
        m_screen_width  = MyScreenWidth;
        m_screen_height = MyScreenHeight;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    FlushQuads(1);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLUIRenderer::Draw()
{
    KFX_ZONE("UIRenderer::Draw");
    // Emit per-frame stats before drawing (capture sizes before clear).
    KFX_PLOT("UI/Quads",      (int)m_ui_quads.size());
    KFX_PLOT("UI/Lines",      (int)m_ui_lines.size());
    // Full draw: back layer then front layer.
    DrawBack();
    DrawFront();
}

void GLUIRenderer::SubmitFBOQuad(int x, int y, int w, int h, GLuint tex_id, float clip_radius)
{
    FBOQuad q;
    q.x0     = (float)x;
    q.y0     = (float)y;
    q.x1     = (float)(x + w);
    q.y1     = (float)(y + h);
    q.tex_id = tex_id;
    q.clip_radius = clip_radius;
    m_fbo_quads.push_back(q);
}



void GLUIRenderer::Clear()
{
    m_ui_quads.clear();
    m_ui_lines.clear();
    m_fbo_quads.clear();
    m_vertices.clear();
    m_minimap_pending = false;
    m_current_layer = 1;  // Reset to front layer (default) each frame
}

// Helper: compile one shader stage. Returns 0 on failure (error already logged).
static GLuint CompileStage(GLenum type, const char* src, const char* label)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, 512, nullptr, log);
        ERRORLOG("Shader compile error (%s): %s", label, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Helper: link vert+frag into a program, set sampler uniforms, return program or 0 on error.
struct SamplerBinding { const char* name; int unit; };
static GLuint LinkProgram(GLuint vert, GLuint frag, const char* label,
                          const SamplerBinding* bindings, int nBindings)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, nullptr, log);
        ERRORLOG("Shader link error (%s): %s", label, log);
        glDeleteProgram(prog);
        return 0;
    }
    glUseProgram(prog);
    for (int i = 0; i < nBindings; ++i)
        glUniform1i(glGetUniformLocation(prog, bindings[i].name), bindings[i].unit);
    glUseProgram(0);
    return prog;
}

bool GLUIRenderer::CreateShaders()
{
    // Compile shared vertex shader once.
    GLuint vert = CompileStage(GL_VERTEX_SHADER, UI_VERTEX_SHADER, "UI_VERTEX");
    if (!vert) return false;

    bool ok = true;

    // --- Sprite program (palette-indexed atlas) ---
    {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_SPRITE_FRAGMENT_SHADER, "UI_SPRITE_FRAG");
        if (!frag) { ok = false; }
        else {
            SamplerBinding bindings[] = {{"u_sprite_atlas", 0}, {"u_palette", 1}};
            m_prog_sprite = LinkProgram(vert, frag, "UI_SPRITE", bindings, 2);
            glDeleteShader(frag);
            if (!m_prog_sprite) ok = false;
            else m_loc_screen_sprite = glGetUniformLocation(m_prog_sprite, "u_screen_size");
        }
    }

    // --- Sprite-colored program (atlas as discard mask, flat vertex colour) ---
    if (ok) {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_SPRITE_COLORED_FRAGMENT_SHADER, "UI_SPRITE_COLORED_FRAG");
        if (!frag) { ok = false; }
        else {
            SamplerBinding bindings[] = {{"u_sprite_atlas", 0}};
            m_prog_sprite_colored = LinkProgram(vert, frag, "UI_SPRITE_COLORED", bindings, 1);
            glDeleteShader(frag);
            if (!m_prog_sprite_colored) ok = false;
            else m_loc_screen_sprite_colored = glGetUniformLocation(m_prog_sprite_colored, "u_screen_size");
        }
    }

    // --- Font program (RGBA glyph atlas) ---
    if (ok) {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_FONT_FRAGMENT_SHADER, "UI_FONT_FRAG");
        if (!frag) { ok = false; }
        else {
            SamplerBinding bindings[] = {{"u_font_atlas", 0}};
            m_prog_font = LinkProgram(vert, frag, "UI_FONT", bindings, 1);
            glDeleteShader(frag);
            if (!m_prog_font) ok = false;
            else m_loc_screen_font = glGetUniformLocation(m_prog_font, "u_screen_size");
        }
    }

    // --- Solid program (no textures) ---
    if (ok) {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_SOLID_FRAGMENT_SHADER, "UI_SOLID_FRAG");
        if (!frag) { ok = false; }
        else {
            m_prog_solid = LinkProgram(vert, frag, "UI_SOLID", nullptr, 0);
            glDeleteShader(frag);
            if (!m_prog_solid) ok = false;
            else m_loc_screen_solid = glGetUniformLocation(m_prog_solid, "u_screen_size");
        }
    }

    // --- Remap program (palette-indexed atlas + fade-table colour remap) ---
    if (ok) {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_REMAP_FRAGMENT_SHADER, "UI_REMAP_FRAG");
        if (!frag) { ok = false; }
        else {
            SamplerBinding bindings[] = {{"u_sprite_atlas", 0}, {"u_palette", 1}, {"u_fade_table", 2}};
            m_prog_remap = LinkProgram(vert, frag, "UI_REMAP", bindings, 3);
            glDeleteShader(frag);
            if (!m_prog_remap) ok = false;
            else {
                m_loc_screen_remap = glGetUniformLocation(m_prog_remap, "u_screen_size");
                m_loc_remap_row    = glGetUniformLocation(m_prog_remap, "u_remap_row");
            }
        }
    }

    // --- FBO/PiP composite program (RGBA8 direct, no palette lookup) ---
    if (ok) {
        GLuint frag = CompileStage(GL_FRAGMENT_SHADER, UI_FBO_FRAGMENT_SHADER, "UI_FBO_FRAG");
        if (!frag) { ok = false; }
        else {
            SamplerBinding bindings[] = {{"u_fbo_tex", 0}};
            m_prog_fbo = LinkProgram(vert, frag, "UI_FBO", bindings, 1);
            glDeleteShader(frag);
            if (!m_prog_fbo) ok = false;
            else m_loc_screen_fbo = glGetUniformLocation(m_prog_fbo, "u_screen_size");
            m_loc_fbo_clip_rect   = glGetUniformLocation(m_prog_fbo, "u_clip_rect");
            m_loc_fbo_clip_radius = glGetUniformLocation(m_prog_fbo, "u_clip_radius");
            m_loc_fbo_clip_scrh   = glGetUniformLocation(m_prog_fbo, "u_clip_screen_h");
        }
    }

    glDeleteShader(vert);
    // Label all successfully-created shader programs (GL_KHR_debug).
    if (m_prog_sprite)         KFX_GL_LABEL(GL_PROGRAM, m_prog_sprite,         "UIR/SpriteProg");
    if (m_prog_sprite_colored) KFX_GL_LABEL(GL_PROGRAM, m_prog_sprite_colored, "UIR/SpriteColoredProg");
    if (m_prog_font)           KFX_GL_LABEL(GL_PROGRAM, m_prog_font,           "UIR/FontProg");
    if (m_prog_solid)          KFX_GL_LABEL(GL_PROGRAM, m_prog_solid,          "UIR/SolidProg");
    if (m_prog_remap)          KFX_GL_LABEL(GL_PROGRAM, m_prog_remap,          "UIR/RemapProg");
    if (m_prog_fbo)            KFX_GL_LABEL(GL_PROGRAM, m_prog_fbo,            "UIR/FBOProg");
    return ok;
}

void GLUIRenderer::CreateVertexArrays()
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    KFX_GL_LABEL(GL_VERTEX_ARRAY, m_vao, "UIR/VAO");
    KFX_GL_LABEL(GL_BUFFER,       m_vbo, "UIR/VBO");
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    
    // Allocate dynamic buffer
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLUIVertex) * 6144, nullptr, GL_DYNAMIC_DRAW);
    
    // Vertex attributes matching UI_VERTEX_SHADER layout
    // Position (vec2) - location 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLUIVertex), (void*)0);
    glEnableVertexAttribArray(0);

    // UV coordinates (vec2) - location 1
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLUIVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color (vec4) - location 2  
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLUIVertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    // Z-depth (float) - location 3
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(GLUIVertex), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    // Mode (float) - location 4
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(GLUIVertex), (void*)(9 * sizeof(float)));
    glEnableVertexAttribArray(4);
    
    glBindVertexArray(0);
}

void GLUIRenderer::FlushQuads(int layer)
{
    // Partition: quads matching 'layer' first, preserving submission order.
    auto mid = std::stable_partition(m_ui_quads.begin(), m_ui_quads.end(),
        [layer](const UIQuad& q) { return (int)q.layer == layer; });
    if (mid == m_ui_quads.begin()) return;  // nothing for this layer

    // Reset texture unit state from whatever GPUFlushNow (or a previous pass) left
    // behind.  The world renderer leaves the tile atlas bound at unit 0 (GL_TEXTURE_2D)
    // and the palette at unit 1 (GL_TEXTURE_2D 256×1) with GL_TEXTURE1 as the active unit.
    // Without this reset the wrong atlas would be sampled by the UI sprite shader.
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Classify a quad's render pass by its mode float.
    // Each pass type uses a different shader and/or texture binding.
    enum PassType { PASS_SLAB, PASS_SOLID, PASS_SPRITE, PASS_COLORED, PASS_FONT, PASS_REMAP };
    auto classify = [](float mode) -> PassType {
        if (mode >= 29.5f) return PASS_REMAP;
        if (mode >= 19.5f) return PASS_COLORED;
        if (mode >= 9.5f)  return PASS_SLAB;
        if (mode >= 1.5f)  return PASS_SOLID;
        if (mode >= 0.5f)  return PASS_FONT;
        return PASS_SPRITE;
    };

    // Track remap_row for the current batch (only relevant for PASS_REMAP).
    int current_remap_row = -1;

    // Bind state for a given pass type and flush accumulated vertices.
    auto flush_batch = [&](PassType pass) {
        if (m_vertices.empty()) return;

        switch (pass) {
        case PASS_SLAB:
            if (!m_slab_texture) { m_vertices.clear(); return; }
            glUseProgram(m_prog_sprite);
            glUniform2f(m_loc_screen_sprite, (float)m_screen_width, (float)m_screen_height);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_slab_texture);
            if (m_palette_texture) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(m_palette_texture_target, m_palette_texture);
            }
            break;

        case PASS_SOLID:
            glUseProgram(m_prog_solid);
            glUniform2f(m_loc_screen_solid, (float)m_screen_width, (float)m_screen_height);
            break;

        case PASS_SPRITE:
            glUseProgram(m_prog_sprite);
            glUniform2f(m_loc_screen_sprite, (float)m_screen_width, (float)m_screen_height);
            if (m_sprite_atlas) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_sprite_atlas->GetTexture());
            }
            if (m_palette_texture) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(m_palette_texture_target, m_palette_texture);
            }
            break;

        case PASS_COLORED:
            if (!m_prog_sprite_colored) { m_vertices.clear(); return; }
            glUseProgram(m_prog_sprite_colored);
            glUniform2f(m_loc_screen_sprite_colored, (float)m_screen_width, (float)m_screen_height);
            if (m_sprite_atlas) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_sprite_atlas->GetTexture());
            }
            break;

        case PASS_FONT:
            glUseProgram(m_prog_font);
            glUniform2f(m_loc_screen_font, (float)m_screen_width, (float)m_screen_height);
            if (m_font_atlas) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_font_atlas->GetTextureID());
            }
            break;

        case PASS_REMAP:
            if (!m_prog_remap || !m_sprite_atlas || !m_palette_texture || !m_fade_texture) {
                m_vertices.clear(); return;
            }
            glUseProgram(m_prog_remap);
            glUniform2f(m_loc_screen_remap, (float)m_screen_width, (float)m_screen_height);
            glUniform1f(m_loc_remap_row, (float)current_remap_row);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_sprite_atlas->GetTexture());
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(m_palette_texture_target, m_palette_texture);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_fade_texture);
            break;
        }

        glBufferData(GL_ARRAY_BUFFER, sizeof(GLUIVertex) * m_vertices.size(), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLUIVertex) * m_vertices.size(), m_vertices.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_vertices.size());
        m_vertices.clear();
    };

    // Walk quads in submission order, batching consecutive same-pass quads.
    // Remap quads also break on remap_row changes (uniform must be updated).
    PassType current_pass = classify(m_ui_quads.begin()->mode);
    current_remap_row = m_ui_quads.begin()->remap_row;
    m_vertices.clear();
    for (auto it = m_ui_quads.begin(); it != mid; ++it) {
        PassType pass = classify(it->mode);
        bool remap_row_changed = (pass == PASS_REMAP && it->remap_row != current_remap_row);
        if (pass != current_pass || remap_row_changed) {
            flush_batch(current_pass);
            current_pass = pass;
            current_remap_row = it->remap_row;
        }
        ExpandQuadToVertices(*it);
    }
    flush_batch(current_pass);

    glBindVertexArray(0);
    m_ui_quads.erase(m_ui_quads.begin(), mid);  // Remove flushed layer-N quads                                                                                                                                                                                                            
}


void GLUIRenderer::FlushLines(int layer)
{
    auto mid = std::stable_partition(m_ui_lines.begin(), m_ui_lines.end(),
        [layer](const UILine& l) { return (int)l.layer == layer; });
    if (mid == m_ui_lines.begin()) return;

    m_vertices.clear();
    for (auto it = m_ui_lines.begin(); it != mid; ++it)
        ExpandLineToVertices(*it);

    if (!m_vertices.empty()) {
        glUseProgram(m_prog_solid);
        glUniform2f(m_loc_screen_solid, (float)m_screen_width, (float)m_screen_height);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GLUIVertex) * m_vertices.size(), nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLUIVertex) * m_vertices.size(), m_vertices.data());
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_vertices.size());
        glBindVertexArray(0);
    }
    m_ui_lines.erase(m_ui_lines.begin(), mid);
}

void GLUIRenderer::ExpandQuadToVertices(const UIQuad& quad)
{
    // Create two triangles for the quad
    GLUIVertex v[6];
    
    // Triangle 1: top-left, bottom-left, top-right
    v[0] = {quad.x0, quad.y0, quad.u0, quad.v0, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    v[1] = {quad.x0, quad.y1, quad.u0, quad.v1, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    v[2] = {quad.x1, quad.y0, quad.u1, quad.v0, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    
    // Triangle 2: top-right, bottom-left, bottom-right
    v[3] = {quad.x1, quad.y0, quad.u1, quad.v0, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    v[4] = {quad.x0, quad.y1, quad.u0, quad.v1, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    v[5] = {quad.x1, quad.y1, quad.u1, quad.v1, quad.r, quad.g, quad.b, quad.a, quad.z, quad.mode};
    
    // Add vertices to buffer
    for (int i = 0; i < 6; ++i) {
        m_vertices.push_back(v[i]);
    }
}

void GLUIRenderer::ExpandLineToVertices(const UILine& line)
{
    // Convert line to thick rectangle
    Vec2f dir(line.x2 - line.x1, line.y2 - line.y1);
    float len = dir.length();

    if (len < 0.001f) return;

    Vec2f perp = (dir / len).perp() * (line.thickness * 0.5f);
    Vec2f p1(line.x1, line.y1);
    Vec2f p2(line.x2, line.y2);

    // Create quad vertices
    GLUIVertex v[6];

    // Triangle 1
    v[0] = {p1.x + perp.x, p1.y + perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};
    v[1] = {p1.x - perp.x, p1.y - perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};
    v[2] = {p2.x + perp.x, p2.y + perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};

    // Triangle 2
    v[3] = {p2.x + perp.x, p2.y + perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};
    v[4] = {p1.x - perp.x, p1.y - perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};
    v[5] = {p2.x - perp.x, p2.y - perp.y, 0.0f, 0.0f, line.r, line.g, line.b, line.a, line.z, 2.0f};
    
    // Add vertices to buffer
    for (int i = 0; i < 6; ++i) {
        m_vertices.push_back(v[i]);
    }
}

void GLUIRenderer::SubmitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, 
                             float r, float g, float b, float a, float z, float mode, uint32_t texture_id)
{
    UIQuad quad;
    quad.x0 = x;
    quad.y0 = y;
    quad.x1 = x + w;
    quad.y1 = y + h;
    quad.u0 = u0;
    quad.v0 = v0;
    quad.u1 = u1;
    quad.v1 = v1;
    quad.r = r;
    quad.g = g;
    quad.b = b;
    quad.a = a;
    quad.z = m_world_depth_active ? m_world_z : z;
    quad.mode = mode;
    quad.texture_id = texture_id;
    quad.remap_row = -1;
    if (m_top_overlay_active)
        quad.layer = 3;
    else if (m_world_depth_active)
        quad.layer = 2;
    else
        quad.layer = static_cast<uint8_t>(m_current_layer);
    
    m_ui_quads.push_back(quad);
}

void GLUIRenderer::SetWorldDepth(float ndc_z)
{
    m_world_z            = ndc_z;
    m_world_depth_active = true;
}

void GLUIRenderer::ClearWorldDepth()
{
    m_world_depth_active = false;
}

void GLUIRenderer::SetTopOverlay()
{
    m_top_overlay_active = true;
}

void GLUIRenderer::ClearTopOverlay()
{
    m_top_overlay_active = false;
}

void GLUIRenderer::SubmitLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a, 
                             float z, float thickness)
{
    UILine line;
    line.x1 = x1;
    line.y1 = y1;
    line.x2 = x2;
    line.y2 = y2;
    line.r = r;
    line.g = g;
    line.b = b;
    line.a = a;
    line.z = m_world_depth_active ? m_world_z : z;
    line.thickness = thickness;
    if (m_top_overlay_active)
        line.layer = 3;
    else if (m_world_depth_active)
        line.layer = 2;
    else
        line.layer = static_cast<uint8_t>(m_current_layer);
    
    m_ui_lines.push_back(line);
}



/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED