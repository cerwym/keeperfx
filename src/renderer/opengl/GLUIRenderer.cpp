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
#include "renderer/RenderThreadManager.h"
#include "bflib_basics.h"
#include "bflib_video.h"       // lbDisplay.DrawFlags (UIAlphaFromDrawFlags), units_per_pixel_best
#include "engine_render.h"     // colored_stripey_lines, hud_scale, line_box_size
#include "globals.h"            // get_gameturn()

#include <glad/glad.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include "renderer/VecMath.h"
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

struct SamplerBinding {
    const char* name;
    int unit;
};

/******************************************************************************/

GLUIRenderer::GLUIRenderer()
    : m_prog_sprite(0)
    , m_prog_font(0)
    , m_prog_solid(0)
    , m_prog_remap(0)
    , m_prog_fbo(0)
    , m_prog_sprite_colored(-1)
    , m_loc_screen_sprite(-1)
    , m_loc_screen_sprite_colored(-1)
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
    m_quads[1].reserve(512);  // layer-1 (front) is the most common
    m_quads[2].reserve(128);  // layer-2 (world-depth)
    m_lines[1].reserve(256);
    m_vertices.reserve(3072);
    m_fbo_quads.reserve(4);
}

GLUIRenderer::~GLUIRenderer()
{
    Shutdown();
}

bool GLUIRenderer::Init()
{
    CreateVertexArrays();
    return true;
}

void GLUIRenderer::Shutdown()
{
    delete[] m_minimap_cpu_buf;
    m_minimap_cpu_buf  = nullptr;
    m_minimap_cpu_size = 0;
    delete[] m_rt_minimap_cpu_buf;
    m_rt_minimap_cpu_buf  = nullptr;
    m_rt_minimap_cpu_size = 0;
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

uint8_t* GLUIRenderer::QuerySpriteMask(SpriteHandle h, int* out_w, int* out_h, int* out_stride)
{
    if (!m_sprite_atlas) return nullptr;
    return m_sprite_atlas->GetSpriteMask(h, out_w, out_h, out_stride);
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

void GLUIRenderer::SetFadeTexture(uint32_t tex)
{
    m_fade_texture = tex;
}

/** Map current layer state to the appropriate IRUILayer enum value. */
static IRUILayer ComputeIRLayer(int current_layer, bool world_depth, bool top_overlay)
{
    if (top_overlay)  return IRUILayer::Overlay;
    if (world_depth)  return IRUILayer::WorldDepth;
    if (current_layer == 0) return IRUILayer::Back;
    return IRUILayer::Front;
}

void GLUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth)
{
    if (!m_palette_data) return;
    float dx = (float)(x2 - x1);
    float dy = (float)(y2 - y1);
    float line_length = std::sqrt(dx * dx + dy * dy);
    if (line_length < 0.001f) return;

    // Scale line thickness to match the software renderer
    float custom_line_box_size = line_box_size / (float)LINE_BOX_SCALE;
    float thickness = custom_line_box_size * units_per_pixel_best / (float)UPP_BASE;
    if (thickness < 1.0f) thickness = 1.0f;
    // Thin slightly when zoomed out, matching the software lerp
    thickness = thickness + (1.0f - thickness) * (1.0f - hud_scale);
    if (thickness < 1.0f) thickness = 1.0f;

    float step = (1.0f + 3.0f * (1.0f - hud_scale)) * ((float)STRIPEY_COLORS / (float)units_per_pixel_best);
    if (step < 0.001f) step = 1.0f;
    float band_width = 1.0f / step;

    if (m_ui_write_cmds) {
        IRUISlabSelectorCmd cmd;
        cmd.layer       = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x1          = x1;
        cmd.y1          = y1;
        cmd.x2          = x2;
        cmd.y2          = y2;
        cmd.colour      = color;
        cmd.z_depth     = z_depth;
        cmd.line_length = line_length;
        cmd.thickness   = thickness;
        cmd.band_width  = band_width;
        cmd.step        = step;
        cmd.game_turn   = (uint32_t)(get_gameturn() & (STRIPEY_COLORS - 1));
        m_ui_write_cmds->slab_selectors.Append(cmd);
        return;
    }

    float ndx = dx / line_length;
    float ndy = dy / line_length;

    // Animated marching-stripe pattern along the line.
    // The starting offset shifts each game turn for the animation.
    float phase = (float)(get_gameturn() & (STRIPEY_COLORS - 1));

    const struct stripey_line* sl = &colored_stripey_lines[color];

    constexpr int MAX_STRIPEY_SEGMENTS = 512;
    float t = 0.0f;
    int segments = 0;
    while (t < line_length && segments < MAX_STRIPEY_SEGMENTS) {
        float anim_pos = phase + t * step;
        anim_pos = std::fmod(anim_pos, (float)STRIPEY_COLORS);
        if (anim_pos < 0.0f) anim_pos += (float)STRIPEY_COLORS;
        int ci = std::max(0, std::min(STRIPEY_COLORS - 1, (int)anim_pos));

        float t_end = std::min(t + band_width, line_length);

        unsigned char pal_idx = sl->stripey_line_color_array[ci];
        float r = m_palette_data[pal_idx * 3 + 0] / VGA6_MAX;
        float g = m_palette_data[pal_idx * 3 + 1] / VGA6_MAX;
        float b = m_palette_data[pal_idx * 3 + 2] / VGA6_MAX;

        float sx = (float)x1 + ndx * t;
        float sy = (float)y1 + ndy * t;
        float ex = (float)x1 + ndx * t_end;
        float ey = (float)y1 + ndy * t_end;

        SubmitLine(sx, sy, ex, ey, r, g, b, 1.0f, z_depth, thickness);

        t = t_end;
        segments++;
    }
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

/** Map IRUILayer to the m_quads[] / m_rt_quads[] array index. */
static int IRUILayerToIndex(IRUILayer layer)
{
    switch (layer) {
    case IRUILayer::Back:       return 0;
    case IRUILayer::Front:      return 1;
    case IRUILayer::WorldDepth: return 2;
    case IRUILayer::Overlay:    return 3;
    default:                    return 1; // Cursor falls back to front
    }
}

void GLUIRenderer::SubmitPanelSprite(int32_t x, int32_t y, int units_per_px, SpriteHandle spr, bool flip_horiz)
{
    if (spr == kInvalidSpriteHandle) {
        static int s_inv = 0; if (s_inv++ < 10) WARNLOG("SubmitPanelSprite: kInvalidSpriteHandle at (%d,%d)", x, y);
        return;
    }
    if (!m_sprite_atlas) {
        static int s_atl = 0; if (s_atl++ < 5) ERRORLOG("SubmitPanelSprite: no sprite atlas");
        return;
    }
    if (m_ui_write_cmds) {
        IRUISpriteCmd cmd;
        cmd.layer        = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x            = x;
        cmd.y            = y;
        cmd.units_per_px = units_per_px;
        cmd.sprite       = spr;
        cmd.flags        = flip_horiz ? kIRSpriteFlipHoriz : 0u;
        cmd.alpha        = UIAlphaFromDrawFlags();
        cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
        m_ui_write_cmds->sprites.Append(cmd);
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) {
        static int s_uv = 0; if (s_uv++ < 20) WARNLOG("SubmitPanelSprite: sprite handle %u not in atlas at (%d,%d)", spr, x, y);
        return;
    }
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
    if (spr == kInvalidSpriteHandle) {
        static int s_inv = 0; if (s_inv++ < 10) WARNLOG("SubmitPanelSpriteRemap: kInvalidSpriteHandle at (%d,%d)", x, y);
        return;
    }
    if (!m_sprite_atlas || !m_fade_texture) {
        static int s_res = 0; if (s_res++ < 5)
            ERRORLOG("SubmitPanelSpriteRemap: GL resources not ready (atlas=%p, fade_tex=%u) — NOT falling back to CPU",
                     (void*)m_sprite_atlas, m_fade_texture);
        return;
    }
    if (m_ui_write_cmds) {
        IRUISpriteRemapCmd cmd;
        cmd.layer        = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x            = x;
        cmd.y            = y;
        cmd.units_per_px = units_per_px;
        cmd.sprite       = spr;
        cmd.remap_row    = remap_row;
        cmd.alpha        = UIAlphaFromDrawFlags();
        cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
        m_ui_write_cmds->sprites_remap.Append(cmd);
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) {
        static int s_uv = 0; if (s_uv++ < 20) WARNLOG("SubmitPanelSpriteRemap: sprite handle %u not in atlas at (%d,%d)", spr, x, y);
        return;
    }
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
    q.remap_row = remap_row;
    const int layer = m_top_overlay_active ? 3
                    : m_world_depth_active ? 2
                    : m_current_layer;
    m_quads[layer].push_back(q);
}

void GLUIRenderer::SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                            SpriteHandle spr, uint8_t color_idx)
{
    if (spr == kInvalidSpriteHandle) {
        static int s_inv = 0; if (s_inv++ < 10) WARNLOG("SubmitPanelSpriteColored: kInvalidSpriteHandle at (%d,%d)", x, y);
        return;
    }
    if (!m_sprite_atlas || !m_palette_data) {
        static int s_res = 0; if (s_res++ < 5)
            ERRORLOG("SubmitPanelSpriteColored: GL resources not ready (atlas=%p, palette=%p)", (void*)m_sprite_atlas, (void*)m_palette_data);
        return;
    }
    if (m_ui_write_cmds) {
        IRUISpriteColoredCmd cmd;
        cmd.layer        = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x            = x;
        cmd.y            = y;
        cmd.units_per_px = units_per_px;
        cmd.sprite       = spr;
        cmd.colour_idx   = color_idx;
        cmd.alpha        = UIAlphaFromDrawFlags();
        cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
        m_ui_write_cmds->sprites_colored.Append(cmd);
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) {
        static int s_uv = 0; if (s_uv++ < 20) WARNLOG("SubmitPanelSpriteColored: sprite handle %u not in atlas at (%d,%d)", spr, x, y);
        return;
    }
    float w = (float)((uv.pixel_w * units_per_px + 8) / 16);
    float h = (float)((uv.pixel_h * units_per_px + 8) / 16);
    float r = m_palette_data[color_idx * 3 + 0] / VGA6_MAX;
    float g = m_palette_data[color_idx * 3 + 1] / VGA6_MAX;
    float b = m_palette_data[color_idx * 3 + 2] / VGA6_MAX;
    // mode=20.0: atlas-as-mask, flat vertex colour (Pass 5 in FlushQuads)
    SubmitQuad((float)x, (float)y, w, h,
               uv.u0, uv.v0, uv.u1, uv.v1,
               r, g, b, UIAlphaFromDrawFlags(), 0.5f, 20.0f);
}

void GLUIRenderer::SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h, SpriteHandle spr)
{
    if (spr == kInvalidSpriteHandle) {
        static int s_inv = 0; if (s_inv++ < 10) WARNLOG("SubmitScaledSprite: kInvalidSpriteHandle at (%d,%d)", x, y);
        return;
    }
    if (!m_sprite_atlas) {
        static int s_atl = 0; if (s_atl++ < 5) ERRORLOG("SubmitScaledSprite: no sprite atlas");
        return;
    }
    if (m_ui_write_cmds) {
        IRUISpriteCmd cmd;
        cmd.layer        = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x            = x;
        cmd.y            = y;
        cmd.w            = w;
        cmd.h            = h;
        cmd.units_per_px = 16;
        cmd.sprite       = spr;
        cmd.flags        = kIRSpriteScaled;
        cmd.alpha        = UIAlphaFromDrawFlags();
        cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
        m_ui_write_cmds->sprites.Append(cmd);
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) {
        static int s_uv = 0; if (s_uv++ < 20) WARNLOG("SubmitScaledSprite: sprite handle %u not in atlas at (%d,%d)", spr, x, y);
        return;
    }
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               uv.u0, uv.v0, uv.u1, uv.v1,
               1.0f, 1.0f, 1.0f, UIAlphaFromDrawFlags(), 0.5f, 0.0f);
}

void GLUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx)
{
    if (!m_palette_data) {
        static int s_pal = 0; if (s_pal++ < 5) ERRORLOG("SubmitSolidBox: no palette data");
        return;
    }
    if (m_ui_write_cmds) {
        IRUISolidBoxCmd cmd;
        cmd.layer      = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x          = x;
        cmd.y          = y;
        cmd.w          = w;
        cmd.h          = h;
        cmd.colour_idx = color_idx;
        cmd.alpha      = 1.0f;
        m_ui_write_cmds->solid_boxes.Append(cmd);
        return;
    }
    float r = m_palette_data[color_idx * 3 + 0] / VGA6_MAX;
    float g = m_palette_data[color_idx * 3 + 1] / VGA6_MAX;
    float b = m_palette_data[color_idx * 3 + 2] / VGA6_MAX;
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f,
               r, g, b, 1.0f, 0.5f, 3.0f);
}

void GLUIRenderer::SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha)
{
    if (!m_palette_data) {
        static int s_pal = 0; if (s_pal++ < 5) ERRORLOG("SubmitSolidBoxAlpha: no palette data");
        return;
    }
    if (m_ui_write_cmds) {
        IRUISolidBoxCmd cmd;
        cmd.layer      = ComputeIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
        cmd.x          = x;
        cmd.y          = y;
        cmd.w          = w;
        cmd.h          = h;
        cmd.colour_idx = color_idx;
        cmd.alpha      = alpha;
        m_ui_write_cmds->solid_boxes.Append(cmd);
        return;
    }
    float r = m_palette_data[color_idx * 3 + 0] / VGA6_MAX;
    float g = m_palette_data[color_idx * 3 + 1] / VGA6_MAX;
    float b = m_palette_data[color_idx * 3 + 2] / VGA6_MAX;
    SubmitQuad((float)x, (float)y, (float)w, (float)h,
               0.0f, 0.0f, 1.0f, 1.0f,
               r, g, b, alpha, 0.5f, 3.0f);
}

void GLUIRenderer::UpdateSlabTexture(const uint8_t* data, int dim)
{
    // Latch the data pointer + dim so FlushPendingInit() can do the GL work on
    // the render thread that owns the context.  No GL calls here — this function
    // is called from the game thread (RendererNotifySpritesReloaded) which no
    // longer holds the GL context after the first EndFrame().
    if (!data || dim <= 0) {
        SYNCLOG("UpdateSlabTexture: REJECTED data=%p dim=%d", (const void*)data, dim);
        return;
    }
    m_slab_pending_data = data;
    m_slab_pending_dim  = dim;
}

void GLUIRenderer::FlushPendingInit()
{
    if (!m_slab_pending_data || m_slab_pending_dim <= 0)
        return;

    const uint8_t* data = m_slab_pending_data;
    int            dim  = m_slab_pending_dim;
    m_slab_pending_data = nullptr;
    m_slab_pending_dim  = 0;

    if (m_slab_texture == 0) glGenTextures(1, &m_slab_texture);
    SYNCLOG("FlushPendingInit/slab: tex=%u dim=%d first8=[%d,%d,%d,%d,%d,%d,%d,%d]",
            m_slab_texture, dim,
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
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
    // Accept a pending upload as ready: FlushPendingInit() (called at the top of
    // EndFrame_GL, before DrawBack) will create the texture before the quad is
    // actually drawn.  Only fall back to the CPU WScreen path when no texture
    // exists AND no upload is pending.
    int dim = (m_slab_dim > 0) ? m_slab_dim : m_slab_pending_dim;
    if (dim <= 0) {
        static int s_slab_miss = 0;
        if (s_slab_miss < 5)
            SYNCLOG("SubmitSlabBackground: SKIP tex=%u dim=%d pending_dim=%d (count=%d)",
                    m_slab_texture, m_slab_dim, m_slab_pending_dim, s_slab_miss);
        ++s_slab_miss;
        return false;
    }
    if (m_ui_write_cmds) {
        IRUISlabBackgroundCmd cmd;
        // Always on Back layer regardless of current layer state.
        cmd.layer = IRUILayer::Back;
        cmd.x     = x;
        cmd.y     = y;
        cmd.w     = w;
        cmd.h     = h;
        m_ui_write_cmds->slab_backgrounds.Append(cmd);
        return true;
    }
    float u1 = (float)w / (float)dim;
    float v1 = (float)h / (float)dim;
    // Always submit on layer 0 (back, rendered before the staging-buffer blit).
    // Force world-depth off so SubmitQuad doesn't redirect to layer 2.
    int saved_layer = m_current_layer;
    bool saved_world_depth = m_world_depth_active;
    bool saved_top_overlay = m_top_overlay_active;
    m_current_layer = 0;
    m_world_depth_active = false;
    m_top_overlay_active = false;
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
    m_world_depth_active = saved_world_depth;
    m_top_overlay_active = saved_top_overlay;
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
    // Mark that the minimap draw path was exercised this frame, even if the data
    // is not yet valid.  This lets the render thread distinguish "temporarily
    // invalid data" from "draw path was never entered" (e.g. parchment map open).
    m_minimap_submitted = true;
    if (size <= 0 || !m_minimap_cpu_buf) return;
    // GL texture creation and upload are deferred to DrawFrontBase() which runs
    // on the render thread — the GL context owner after the Phase-3A handoff.
    m_minimap_x       = screen_x;
    m_minimap_y       = screen_y;
    m_minimap_size    = size;
    m_minimap_pending = true;
}

void GLUIRenderer::SetLayer(int layer)
{
    m_current_layer = layer;
}

void GLUIRenderer::FlipBuffers()
{
    // Move game-thread quad/line state into render-thread shadow copies.
    // After the move, game-side containers are empty and ready for the next frame.
    // m_fbo_quads is not moved — it is pushed AND consumed on the render thread
    // (SubmitFBOQuad is called during the PiP loop inside EndFrame_GL).
    for (int i = 0; i < 4; ++i) {
        m_rt_quads[i] = std::move(m_quads[i]);
        m_rt_lines[i] = std::move(m_lines[i]);
    }

    // Swap minimap CPU buffers so the render thread reads the just-completed
    // frame's pixel data while the game thread fills the next frame's buffer
    // independently — no aliasing under Phase 3C async execution.
    std::swap(m_minimap_cpu_buf,  m_rt_minimap_cpu_buf);
    std::swap(m_minimap_cpu_size, m_rt_minimap_cpu_size);
    m_rt_minimap_x         = m_minimap_x;
    m_rt_minimap_y         = m_minimap_y;
    m_rt_minimap_size      = m_minimap_size;
    m_rt_minimap_pending   = m_minimap_pending;
    m_rt_minimap_submitted = m_minimap_submitted;
    m_minimap_pending      = false;
    m_minimap_submitted    = false;
}

void GLUIRenderer::DrawBack()
{
    KFX_ZONE("UIRenderer::DrawBack");
    KFX_GPU_ZONE("UIPass::Back");
    KFX_GL_SCOPE(back_grp, "UIPass/Back");

    // Render only layer-0 (back) quads — the sidebar background panels that must land
    // beneath the CPU staging-buffer blit.  Layer-0 is populated by draw_whole_status_panel()
    // via UIRenderer_SetLayer(0) in draw_2d_elements(), redraw_creature_view(), and
    // ParchmentScene::draw().  If no sidebar is visible this vector is legitimately empty.
    if (m_rt_quads[0].empty())
    {
        // Ensure both blend and depth-test are clean regardless of what the world
        // pass may have left behind (gpu_execute_passes early-return leaves both
        // states indeterminate when draw_cmds is empty).
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    FlushQuads_RT(0);

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
    // DrawFront() guarantees scissor is disabled before calling this; disable again
    // defensively in case this is called from another context.
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
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void GLUIRenderer::DrawFront()
{
    KFX_ZONE("UIRenderer::DrawFront");
    KFX_GPU_ZONE("UIPass::Front");
    KFX_GL_SCOPE(front_grp, "UIPass/Front");

    if (m_rt_quads[0].empty() && m_rt_quads[1].empty() && m_rt_quads[2].empty() && m_rt_quads[3].empty()
        && m_rt_lines[0].empty() && m_rt_lines[1].empty() && m_rt_lines[2].empty() && m_rt_lines[3].empty()
        && !m_rt_minimap_pending && !(m_rt_minimap_submitted && m_minimap_texture != 0 && m_rt_minimap_size > 0)
        && m_fbo_quads.empty())
        return;

    // Guarantee full-screen viewport.  The PiP path leaves the viewport at
    // pip_w×pip_h after FlushPiPSprites(); without this reset every draw call
    // below would be clipped to the tiny pip-sized scissor region.
    glViewport(0, 0, m_screen_width, m_screen_height);
    // Ensure scissor is off and default framebuffer is bound before UI draws.
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // FBO/PiP composite quads drawn first so frame-corner sprites sit on top.
    // FlushFBOQuads_impl manages its own GL_BLEND state (enable on entry, disable on exit).
    FlushFBOQuads_impl(m_fbo_quads, m_prog_fbo, m_loc_screen_fbo,
                       m_vao, m_vbo, m_screen_width, m_screen_height,
                       m_loc_fbo_clip_rect, m_loc_fbo_clip_radius, m_loc_fbo_clip_scrh);
    m_fbo_quads.clear();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Flush all remaining (layer-1 / front) quads.
    FlushQuads_RT(1);

    // Minimap: palette-indexed R8 texture — front layer.
    // SubmitMinimap() defers all GL work here (render thread owns GL context).
    // When m_rt_minimap_pending is true, upload fresh pixels then draw.
    // When it is false but a texture already exists, redraw with the cached
    // texture to prevent flickering on frames where pixel submission was skipped.
    // m_rt_minimap_submitted gates all of this: if SubmitMinimap() was not called
    // this frame the minimap draw path was not active (e.g. parchment map open).
    if (m_rt_minimap_submitted && (m_rt_minimap_pending || (m_minimap_texture != 0 && m_rt_minimap_size > 0)))
    {
        if (m_rt_minimap_pending)
        {
            int mm_size = m_rt_minimap_size;
            if (mm_size != m_minimap_tex_size)
            {
                if (m_minimap_texture)
                    glDeleteTextures(1, &m_minimap_texture);
                glGenTextures(1, &m_minimap_texture);
                glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, mm_size, mm_size, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
                m_minimap_tex_size = mm_size;
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
            }
            if (m_rt_minimap_cpu_buf)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mm_size, mm_size, GL_RED, GL_UNSIGNED_BYTE, m_rt_minimap_cpu_buf);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            m_rt_minimap_pending = false;
        }

        float mx = (float)m_rt_minimap_x;
        float my = (float)m_rt_minimap_y;
        float ms = (float)m_rt_minimap_size;

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
        // Restore active unit: palette bind above may have left it on GL_TEXTURE1.
        glActiveTexture(GL_TEXTURE0);
    }

    // Flush slab-selector lines and other line geometry (front layer).
    FlushLines_RT(1);

    // Flush world-depth-tagged elements (layer 2) — these are non-spatial sprites
    // (status flowers, room flags, slab selectors, floating numbers) submitted via
    // SetWorldDepth()/ClearWorldDepth() during draw_nonspatial_sprites_gpu().  They
    // carry an NDC z matching their bucket depth, so the existing tile depth buffer
    // will correctly occlude anything behind a wall.
    {
        if (!m_rt_quads[2].empty() || !m_rt_lines[2].empty())
        {
            // Scissor-clip layer-2 sprites to the game viewport so they can't
            // bleed onto the sidebar, overhead map, or zoom box.  These UI
            // elements don't write depth, so depth testing alone is insufficient.
            if (m_game_vp_set && m_game_vp_w > 0 && m_game_vp_h > 0)
            {
                glEnable(GL_SCISSOR_TEST);
                // GL scissor origin is bottom-left; m_game_vp_y is top-left.
                int scissor_y = m_screen_height - m_game_vp_y - m_game_vp_h;
                glScissor(m_game_vp_x, scissor_y, m_game_vp_w, m_game_vp_h);
            }
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);   // depth writes ON — shaders discard transparent
                                    // fragments, so only opaque pixels write depth.
                                    // The GPU handles occlusion natively; no painter's
                                    // algorithm or mode-ordered passes needed.
            FlushQuads_RT(2);
            FlushLines_RT(2);
            glDisable(GL_DEPTH_TEST);
            if (m_game_vp_set)
                glDisable(GL_SCISSOR_TEST);
        }
    }

    // Layer 3: top-overlay — cursor-driven affordances (slab selector) drawn dead-last,
    // depth test OFF, so they are never obscured by any world or UI element.
    FlushQuads_RT(3);
    FlushLines_RT(3);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLUIRenderer::DrawFrontBase()
{
    KFX_ZONE("UIRenderer::DrawFrontBase");
    KFX_GL_SCOPE(front_base_grp, "UIPass/FrontBase");

    glViewport(0, 0, m_screen_width, m_screen_height);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // FlushFBOQuads_impl manages its own GL_BLEND state (enable on entry, disable on exit).
    FlushFBOQuads_impl(m_fbo_quads, m_prog_fbo, m_loc_screen_fbo,
                       m_vao, m_vbo, m_screen_width, m_screen_height,
                       m_loc_fbo_clip_rect, m_loc_fbo_clip_radius, m_loc_fbo_clip_scrh);
    m_fbo_quads.clear();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    FlushQuads_RT(1);

    // SubmitMinimap() defers all GL work here (render thread owns GL context).
    // When m_rt_minimap_pending is true, upload fresh pixels then draw.
    // When it is false but a texture already exists, redraw with the cached
    // texture to prevent flickering on frames where pixel submission was skipped.
    // m_rt_minimap_submitted gates all of this: if SubmitMinimap() was not called
    // this frame the minimap draw path was not active (e.g. parchment map open).
    if (m_rt_minimap_submitted && (m_rt_minimap_pending || (m_minimap_texture != 0 && m_rt_minimap_size > 0)))
    {
        if (m_rt_minimap_pending)
        {
            int mm_size = m_rt_minimap_size;
            if (mm_size != m_minimap_tex_size)
            {
                if (m_minimap_texture)
                    glDeleteTextures(1, &m_minimap_texture);
                glGenTextures(1, &m_minimap_texture);
                glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, mm_size, mm_size, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
                m_minimap_tex_size = mm_size;
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, m_minimap_texture);
            }
            if (m_rt_minimap_cpu_buf)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mm_size, mm_size, GL_RED, GL_UNSIGNED_BYTE, m_rt_minimap_cpu_buf);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            m_rt_minimap_pending = false;
        }

        float mx = (float)m_rt_minimap_x;
        float my = (float)m_rt_minimap_y;
        float ms = (float)m_rt_minimap_size;

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
        // Restore active unit: palette bind above may have left it on GL_TEXTURE1.
        glActiveTexture(GL_TEXTURE0);
    }

    FlushLines_RT(1);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
    glDisable(GL_BLEND);
}

void GLUIRenderer::DrawFrontOverlay()
{
    KFX_ZONE("UIRenderer::DrawFrontOverlay");
    KFX_GL_SCOPE(front_ovl_grp, "UIPass/FrontOverlay");

    // Guarantee scissor is off before this function's draws. DrawFront() and
    // DrawFrontBase() both do this defensively; match that discipline so layer-3
    // flushes (tooltip, corner frames) are never clipped by a stale scissor rect.
    glDisable(GL_SCISSOR_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    {
        if (!m_rt_quads[2].empty() || !m_rt_lines[2].empty())
        {
            if (m_game_vp_set && m_game_vp_w > 0 && m_game_vp_h > 0)
            {
                glEnable(GL_SCISSOR_TEST);
                int scissor_y = m_screen_height - m_game_vp_y - m_game_vp_h;
                glScissor(m_game_vp_x, scissor_y, m_game_vp_w, m_game_vp_h);
            }
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            FlushQuads_RT(2);
            FlushLines_RT(2);
            glDisable(GL_DEPTH_TEST);
            if (m_game_vp_set)
                glDisable(GL_SCISSOR_TEST);
        }
    }

    FlushQuads_RT(3);
    FlushLines_RT(3);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLUIRenderer::BeginPiPSprites()
{
    for (int i = 0; i < 4; ++i) {
        m_pip_quad_wm[i] = (int)m_quads[i].size();
        m_pip_line_wm[i] = (int)m_lines[i].size();
    }
    m_pip_capture_active = true;
}

void GLUIRenderer::DrawPiPSprites(int pip_w, int pip_h)
{
    if (!m_pip_capture_active)
        return;
    m_pip_capture_active = false;

    // Count total pip-sourced entries across layers 1 and 2 (layers 0/3 never
    // submitted during draw_view, so their tails are expected to be zero).
    int nq = 0, nl = 0;
    for (int i = 0; i < 4; ++i) {
        nq += (int)m_quads[i].size() - m_pip_quad_wm[i];
        nl += (int)m_lines[i].size() - m_pip_line_wm[i];
    }

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

        // For each layer, extract pip-tail into a temporary vector, swap it in as
        // the active queue, flush, then restore the pre-PiP entries.
        std::vector<UIQuad> pip_quads[4];
        std::vector<UILine> pip_lines[4];
        for (int i = 0; i < 4; ++i) {
            pip_quads[i].assign(m_quads[i].begin() + m_pip_quad_wm[i], m_quads[i].end());
            pip_lines[i].assign(m_lines[i].begin() + m_pip_line_wm[i], m_lines[i].end());
            m_quads[i].erase(m_quads[i].begin() + m_pip_quad_wm[i], m_quads[i].end());
            m_lines[i].erase(m_lines[i].begin() + m_pip_line_wm[i], m_lines[i].end());
            m_quads[i].swap(pip_quads[i]);
            m_lines[i].swap(pip_lines[i]);
        }

        // Layer-2: creature status / gold text — depth-tested against FBO geometry.
        if (!m_quads[2].empty() || !m_lines[2].empty())
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            FlushQuads(2);
            FlushLines(2);
            glDisable(GL_DEPTH_TEST);
        }
        // Layer-1: room flags — always on top inside the zoom box.
        if (!m_quads[1].empty() || !m_lines[1].empty())
        {
            glDisable(GL_DEPTH_TEST);
            FlushQuads(1);
            FlushLines(1);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glUseProgram(0);
        m_screen_width  = saved_w;
        m_screen_height = saved_h;

        // Discard any remaining pip entries (layer-0/3 stray quads, should be empty).
        for (int i = 0; i < 4; ++i) { m_quads[i].clear(); m_lines[i].clear(); }

        // Restore the saved pre-PiP entries as the active queues.
        for (int i = 0; i < 4; ++i) {
            m_quads[i].swap(pip_quads[i]);
            m_lines[i].swap(pip_lines[i]);
        }
    }
}

void GLUIRenderer::DrawCursorSprites()
{
    // Draw atlas-quad sprites submitted via SubmitCursorPanelSprite().
    // Uses m_cursor_quads (render-thread-only) instead of m_quads[1], preventing
    // the Phase-3C race where the game thread concurrently pushes to m_quads[1].
    if (m_cursor_quads.empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    flush_quads_from(m_cursor_quads);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void GLUIRenderer::DrawWorldSprites()
{
    // Flush any world-depth sprites (layer 2) submitted via SetWorldDepth() since
    // the last call.  Called from the render thread once per CMD_SPRITES bucket,
    // after draw_3d_sprites_for_bucket() has pushed all sprite quads for that depth.
    // Depth test is already active (set up by GLWorldViewRenderer geometry pass);
    // enable blend here since the geometry pass disables it after the shadow sub-pass.
    if (m_quads[2].empty() && m_lines[2].empty()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    flush_quads_from(m_quads[2]);
    flush_lines_from(m_lines[2]);
    m_quads[2].clear();
    m_lines[2].clear();
    glDisable(GL_BLEND);
}

void GLUIRenderer::SubmitCursorPanelSprite(int32_t x, int32_t y, int units_per_px, SpriteHandle spr)
{
    // Render-thread-only path: pushes to m_cursor_quads instead of m_quads[layer].
    // Called exclusively from GLCursorLayer::Draw() on the render thread so the
    // game thread never touches m_cursor_quads.
    if (spr == kInvalidSpriteHandle) {
        static int s_inv = 0; if (s_inv++ < 10) WARNLOG("SubmitCursorPanelSprite: kInvalidSpriteHandle at (%d,%d)", x, y);
        return;
    }
    if (!m_sprite_atlas) {
        static int s_atl = 0; if (s_atl++ < 5) ERRORLOG("SubmitCursorPanelSprite: no sprite atlas");
        return;
    }
    SpriteUV uv;
    if (!m_sprite_atlas->GetUV(spr, uv)) {
        static int s_uv = 0; if (s_uv++ < 20) WARNLOG("SubmitCursorPanelSprite: sprite handle %u not in atlas at (%d,%d)", spr, x, y);
        return;
    }
    float w = (float)((uv.pixel_w * units_per_px + 8) / 16);
    float h = (float)((uv.pixel_h * units_per_px + 8) / 16);
    UIQuad q;
    q.x0 = (float)x;       q.y0 = (float)y;
    q.x1 = (float)x + w;  q.y1 = (float)y + h;
    q.u0 = uv.u0;          q.v0 = uv.v0;
    q.u1 = uv.u1;          q.v1 = uv.v1;
    q.r = 1.0f;  q.g = 1.0f;  q.b = 1.0f;
    q.a = UIAlphaFromDrawFlags();
    q.z = 0.5f;
    q.mode = 0.0f;   // PASS_SPRITE
    q.texture_id = 0;
    q.remap_row = -1;
    m_cursor_quads.push_back(q);
}

void GLUIRenderer::Draw()
{
    KFX_ZONE("UIRenderer::Draw");
    // Emit per-frame stats before drawing (capture sizes from the render-thread copies).
    KFX_PLOT("UI/Quads",      (int)(m_rt_quads[0].size()+m_rt_quads[1].size()+m_rt_quads[2].size()+m_rt_quads[3].size()));
    KFX_PLOT("UI/Lines",      (int)(m_rt_lines[0].size()+m_rt_lines[1].size()+m_rt_lines[2].size()+m_rt_lines[3].size()));
    // Full draw: back layer then front layer.
    DrawBack();
    DrawFront();
}

void GLUIRenderer::SubmitFBOQuad(int x, int y, int w, int h, uint32_t tex_id, float clip_radius)
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
    for (int i = 0; i < 4; ++i) { m_quads[i].clear(); m_lines[i].clear(); }
    // NOTE: m_fbo_quads and m_vertices are render-thread-only temporaries.
    // m_fbo_quads is populated by SubmitFBOQuad() on the render thread and
    // cleared by DrawFrontBase().  m_vertices is a scratch buffer owned by
    // flush_quads_from() / flush_lines_from().  Clearing either here from the
    // game thread would race with the render thread's EndFrame_GL() work.
    //
    // m_minimap_pending is intentionally NOT reset here. Its lifecycle:
    //   - Set to true: SubmitMinimap() on the game thread.
    //   - Set to false: FlipBuffers() on the game thread, after copying to m_rt_minimap_pending.
    // Clear() is called only from BeginFrame() on the game thread; it is NOT called
    // from the render thread (see comment in RendererOpenGL.cpp near platform_swap_gl_buffers).
    m_current_layer = 1;  // Reset to front layer (default) each frame
    m_world_depth_active = false;   // Safety: ensure flags are clear at frame start
    m_top_overlay_active = false;
    m_game_vp_set = false;
}

void GLUIRenderer::SetGameViewport(int x, int y, int w, int h)
{
    m_game_vp_x   = x;
    m_game_vp_y   = y;
    m_game_vp_w   = w;
    m_game_vp_h   = h;
    m_game_vp_set = true;
    if (m_ui_write_cmds) {
        m_ui_write_cmds->game_vp = { x, y, w, h, true };
    }
}

void GLUIRenderer::SetUICommandBuffers(UICommandBuffers* cmds)
{
    m_ui_write_cmds = cmds;
    if (cmds) cmds->ir_active = true;
}

void GLUIRenderer::ExecuteUIFromIR(const UICommandBuffers& cmds, const FrameState& fs)
{
    // Skip if this frame was a stale-replay (fade-cache) frame — m_rt_quads[] already
    // contain the preserved quads that FlipBuffers() moved; leave them intact.
    if (!cmds.ir_active) return;

    // Clear render-thread quad/line buffers and rebuild from IR commands.
    for (int i = 0; i < 4; ++i) { m_rt_quads[i].clear(); m_rt_lines[i].clear(); }

    SYNCDBG(1, "ExecuteUIFromIR: sprites=%zu solid=%zu slabbg=%zu (atlas=%s)",
            cmds.sprites.Size(), cmds.solid_boxes.Size(), cmds.slab_backgrounds.Size(),
            m_sprite_atlas ? "ok" : "NULL");

    // Restore game viewport if it was captured this frame.
    if (cmds.game_vp.set) {
        m_game_vp_x   = cmds.game_vp.x;
        m_game_vp_y   = cmds.game_vp.y;
        m_game_vp_w   = cmds.game_vp.w;
        m_game_vp_h   = cmds.game_vp.h;
        m_game_vp_set = true;
    } else {
        m_game_vp_set = false;
    }

    const uint8_t* pal = fs.palette; // 768-byte VGA6 palette snapshot

    // ── Solid boxes ─────────────────────────────────────────────────────────
    for (const auto& cmd : cmds.solid_boxes) {
        const int idx = IRUILayerToIndex(cmd.layer);
        float r = pal[cmd.colour_idx * 3 + 0] / VGA6_MAX;
        float g = pal[cmd.colour_idx * 3 + 1] / VGA6_MAX;
        float b = pal[cmd.colour_idx * 3 + 2] / VGA6_MAX;
        UIQuad q;
        q.x0 = (float)cmd.x;         q.y0 = (float)cmd.y;
        q.x1 = (float)(cmd.x+cmd.w); q.y1 = (float)(cmd.y+cmd.h);
        q.u0 = 0.0f; q.v0 = 0.0f; q.u1 = 1.0f; q.v1 = 1.0f;
        q.r = r; q.g = g; q.b = b; q.a = cmd.alpha;
        q.z = 0.5f; q.mode = 3.0f; q.texture_id = 0; q.remap_row = -1;
        m_rt_quads[idx].push_back(q);
    }

    // ── Slab backgrounds ────────────────────────────────────────────────────
    if (m_slab_texture > 0) {
        for (const auto& cmd : cmds.slab_backgrounds) {
            const int idx = IRUILayerToIndex(cmd.layer);
            const int dim = m_slab_dim;
            if (dim <= 0) continue;
            float u1 = (float)cmd.w / (float)dim;
            float v1 = (float)cmd.h / (float)dim;
            // Opaque backing quad
            UIQuad qbg;
            qbg.x0=(float)cmd.x; qbg.y0=(float)cmd.y;
            qbg.x1=(float)(cmd.x+cmd.w); qbg.y1=(float)(cmd.y+cmd.h);
            qbg.u0=0.0f; qbg.v0=0.0f; qbg.u1=1.0f; qbg.v1=1.0f;
            qbg.r=0.0f; qbg.g=0.0f; qbg.b=0.0f; qbg.a=1.0f;
            qbg.z=0.48f; qbg.mode=3.0f; qbg.texture_id=0; qbg.remap_row=-1;
            m_rt_quads[idx].push_back(qbg);
            // Tiled slab texture
            UIQuad qt;
            qt.x0=(float)cmd.x; qt.y0=(float)cmd.y;
            qt.x1=(float)(cmd.x+cmd.w); qt.y1=(float)(cmd.y+cmd.h);
            qt.u0=0.0f; qt.v0=0.0f; qt.u1=u1; qt.v1=v1;
            qt.r=1.0f; qt.g=1.0f; qt.b=1.0f; qt.a=1.0f;
            qt.z=0.49f; qt.mode=10.0f; qt.texture_id=0; qt.remap_row=-1;
            m_rt_quads[idx].push_back(qt);
        }
    }

    // ── Panel sprites ────────────────────────────────────────────────────────
    if (m_sprite_atlas) {
        for (const auto& cmd : cmds.sprites) {
            SpriteUV uv;
            if (!m_sprite_atlas->GetUV(cmd.sprite, uv)) continue;
            const int idx = IRUILayerToIndex(cmd.layer);
            float w, h;
            if (cmd.flags & kIRSpriteScaled) {
                w = (float)cmd.w;
                h = (float)cmd.h;
            } else {
                w = (float)((uv.pixel_w * cmd.units_per_px + 8) / 16);
                h = (float)((uv.pixel_h * cmd.units_per_px + 8) / 16);
            }
            bool flip = (cmd.flags & kIRSpriteFlipHoriz) != 0;
            float u0 = flip ? uv.u1 : uv.u0;
            float u1 = flip ? uv.u0 : uv.u1;
            UIQuad q;
            q.x0=(float)cmd.x; q.y0=(float)cmd.y;
            q.x1=(float)cmd.x+w; q.y1=(float)cmd.y+h;
            q.u0=u0; q.v0=uv.v0; q.u1=u1; q.v1=uv.v1;
            q.r=1.0f; q.g=1.0f; q.b=1.0f; q.a=cmd.alpha;
            q.z=cmd.ndc_z; q.mode=0.0f; q.texture_id=0; q.remap_row=-1;
            m_rt_quads[idx].push_back(q);
        }

        // ── Remap sprites ────────────────────────────────────────────────────
        if (m_fade_texture) {
            for (const auto& cmd : cmds.sprites_remap) {
                SpriteUV uv;
                if (!m_sprite_atlas->GetUV(cmd.sprite, uv)) continue;
                const int idx = IRUILayerToIndex(cmd.layer);
                float w = (float)((uv.pixel_w * cmd.units_per_px + 8) / 16);
                float h = (float)((uv.pixel_h * cmd.units_per_px + 8) / 16);
                UIQuad q;
                q.x0=(float)cmd.x; q.y0=(float)cmd.y;
                q.x1=(float)cmd.x+w; q.y1=(float)cmd.y+h;
                q.u0=uv.u0; q.v0=uv.v0; q.u1=uv.u1; q.v1=uv.v1;
                q.r=1.0f; q.g=1.0f; q.b=1.0f; q.a=cmd.alpha;
                q.z=cmd.ndc_z; q.mode=30.0f; q.texture_id=0; q.remap_row=cmd.remap_row;
                m_rt_quads[idx].push_back(q);
            }
        }

        // ── Coloured sprites ─────────────────────────────────────────────────
        for (const auto& cmd : cmds.sprites_colored) {
            SpriteUV uv;
            if (!m_sprite_atlas->GetUV(cmd.sprite, uv)) continue;
            const int idx = IRUILayerToIndex(cmd.layer);
            float w = (float)((uv.pixel_w * cmd.units_per_px + 8) / 16);
            float h = (float)((uv.pixel_h * cmd.units_per_px + 8) / 16);
            float r = pal[cmd.colour_idx * 3 + 0] / VGA6_MAX;
            float g = pal[cmd.colour_idx * 3 + 1] / VGA6_MAX;
            float b = pal[cmd.colour_idx * 3 + 2] / VGA6_MAX;
            UIQuad q;
            q.x0=(float)cmd.x; q.y0=(float)cmd.y;
            q.x1=(float)cmd.x+w; q.y1=(float)cmd.y+h;
            q.u0=uv.u0; q.v0=uv.v0; q.u1=uv.u1; q.v1=uv.v1;
            q.r=r; q.g=g; q.b=b; q.a=cmd.alpha;
            q.z=cmd.ndc_z; q.mode=20.0f; q.texture_id=0; q.remap_row=-1;
            m_rt_quads[idx].push_back(q);
        }
    }

    // ── Slab selectors ───────────────────────────────────────────────────────
    // NOTE: We write DIRECTLY to m_rt_lines[] (render-thread read buffer) rather
    // than calling SubmitLine() which writes to m_lines[] (game-thread write buffer).
    // SubmitLine() must never be called from ExecuteUIFromIR (render thread) because
    // m_lines[] is concurrently cleared by Clear() on the game thread.
    // Use the per-frame palette snapshot (pal) — not the live m_palette_data pointer —
    // to avoid a data race with LbPaletteSet() on the game thread.
    for (const auto& cmd : cmds.slab_selectors) {
        float dx = (float)(cmd.x2 - cmd.x1);
        float dy = (float)(cmd.y2 - cmd.y1);
        if (cmd.line_length < 0.001f) continue;
        float ndx = dx / cmd.line_length;
        float ndy = dy / cmd.line_length;
        float phase = (float)cmd.game_turn;
        const struct stripey_line* sl = &colored_stripey_lines[cmd.colour];
        const int layer_idx = IRUILayerToIndex(cmd.layer);
        constexpr int MAX_SEG = 512;
        float t = 0.0f;
        int segs = 0;
        while (t < cmd.line_length && segs < MAX_SEG) {
            float anim_pos = phase + t * cmd.step;
            anim_pos = std::fmod(anim_pos, (float)STRIPEY_COLORS);
            if (anim_pos < 0.0f) anim_pos += (float)STRIPEY_COLORS;
            int ci = std::max(0, std::min(STRIPEY_COLORS-1, (int)anim_pos));
            float t_end = std::min(t + cmd.band_width, cmd.line_length);
            unsigned char pi = sl->stripey_line_color_array[ci];
            float r = pal[pi*3+0]/VGA6_MAX;
            float g = pal[pi*3+1]/VGA6_MAX;
            float b = pal[pi*3+2]/VGA6_MAX;
            float sx = (float)cmd.x1 + ndx*t;
            float sy = (float)cmd.y1 + ndy*t;
            float ex = (float)cmd.x1 + ndx*t_end;
            float ey = (float)cmd.y1 + ndy*t_end;
            UILine ln;
            ln.x1 = sx; ln.y1 = sy; ln.x2 = ex; ln.y2 = ey;
            ln.r = r;   ln.g = g;   ln.b = b;   ln.a = 1.0f;
            ln.z = cmd.z_depth;
            ln.thickness = cmd.thickness;
            m_rt_lines[layer_idx].push_back(ln);
            t = t_end; ++segs;
        }
    }

}

void GLUIRenderer::PopulateFromIR(const UICommandBuffers& cmds, const FrameState& fs)
{
    ExecuteUIFromIR(cmds, fs);
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

bool GLUIRenderer::CompileShaders()
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
    flush_quads_from(m_quads[layer]);
}

void GLUIRenderer::flush_quads_from(std::vector<UIQuad>& quads)
{
    if (quads.empty()) return;

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
    PassType current_pass = classify(quads.front().mode);
    current_remap_row = quads.front().remap_row;
    m_vertices.clear();

    for (const auto& q : quads) {
        PassType pass = classify(q.mode);
        bool remap_row_changed = (pass == PASS_REMAP && q.remap_row != current_remap_row);
        if (pass != current_pass || remap_row_changed) {
            flush_batch(current_pass);
            current_pass = pass;
            current_remap_row = q.remap_row;
        }
        ExpandQuadToVertices(q);
    }

    flush_batch(current_pass);

    // Restore texture-unit state to a known baseline.
    // PASS_REMAP binds m_fade_texture on unit 2; PASS_SPRITE/SLAB/REMAP bind
    // m_palette_texture on unit 1.  Leave all units unbound and unit 0 active
    // so the caller's texture state is not corrupted.
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(0);
    quads.clear();
}


void GLUIRenderer::FlushLines(int layer)
{
    flush_lines_from(m_lines[layer]);
}

void GLUIRenderer::flush_lines_from(std::vector<UILine>& lines)
{
    if (lines.empty()) return;

    m_vertices.clear();
    for (const auto& l : lines)
        ExpandLineToVertices(l);

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
    lines.clear();
}

void GLUIRenderer::FlushQuads_RT(int layer)
{
    // Phase 3C: operate directly on m_rt_quads so we never touch m_quads[layer],
    // which the game thread may be concurrently writing for the next frame.
    flush_quads_from(m_rt_quads[layer]);
}

void GLUIRenderer::FlushLines_RT(int layer)
{
    // Phase 3C: operate directly on m_rt_lines so we never touch m_lines[layer].
    flush_lines_from(m_rt_lines[layer]);
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
    const int layer = m_top_overlay_active ? 3
                    : m_world_depth_active ? 2
                    : m_current_layer;
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
    m_quads[layer].push_back(quad);
}

void GLUIRenderer::SetWorldDepth(float ndc_z)
{
    // Guard: render thread must not modify m_world_depth_active — the game thread
    // reads it concurrently in ComputeIRLayer() during Phase 3C execution.
    // Render-thread callers (GLWorldViewRenderer::GPURenderNow) no longer call
    // this function; the guard is a safety net for any future inadvertent call.
    if (g_on_render_thread) return;
    m_world_z            = ndc_z;
    m_world_depth_active = true;
}

void GLUIRenderer::ClearWorldDepth()
{
    if (g_on_render_thread) return;
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
    const int layer = m_top_overlay_active ? 3
                    : m_world_depth_active ? 2
                    : m_current_layer;
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
    m_lines[layer].push_back(line);
}



/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED