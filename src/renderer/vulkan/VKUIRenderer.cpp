/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKUIRenderer.cpp
 *     Vulkan implementation of IUIRenderer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKUIRenderer.h"

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include "renderer/RendererThread.h"
#include "renderer/VecMath.h"
#include "renderer/ir/UICommands.h"
#include "renderer/FrameState.h"
#include "bflib_basics.h"
#include "bflib_video.h"       // lbDisplay, units_per_pixel_best, UPP_BASE
#include "engine_render.h"     // colored_stripey_lines, hud_scale, line_box_size
#include "globals.h"           // get_gameturn()
#include <cstring>
#include <cmath>
#include <algorithm>
#include "post_inc.h"

/******************************************************************************/

static constexpr int   kVKFramesInFlight = 2;
static constexpr float VGA6_MAX          = 63.0f;

/******************************************************************************/
// Construction / Init
/******************************************************************************/

VKUIRenderer::VKUIRenderer()
{
    for (int i = 0; i < 5; ++i) {
        m_rt_quads[i].reserve(256);
        m_rt_lines[i].reserve(64);
    }
    m_fbo_quads.reserve(4);
}

VKUIRenderer::~VKUIRenderer()
{
    Shutdown();
}

bool VKUIRenderer::Init(VkDevice device, VmaAllocator allocator,
                        VKPipelineCache* pipelines,
                        VKDescriptorLayout* descriptors,
                        VkDescriptorSetLayout set_layout)
{
    m_device      = device;
    m_allocator   = allocator;
    m_pipelines   = pipelines;
    m_descriptors = descriptors;
    m_set_layout  = set_layout;

    // Allocate kVKFramesInFlight vertex windows in a single buffer.
    const VkDeviceSize buf_bytes = (VkDeviceSize)(kVKUIMaxVertices * sizeof(UIVertex) * kVKFramesInFlight);
    VkBufferCreateInfo buf_info  = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size  = buf_bytes;
    buf_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_result = {};
    if (vmaCreateBuffer(m_allocator, &buf_info, &alloc_info,
                        &m_vtx_buf, &m_vtx_alloc, &alloc_result) != VK_SUCCESS) {
        ERRORLOG("VKUIRenderer: vmaCreateBuffer (vertex) failed");
        return false;
    }
    m_vtx_mapped = static_cast<UIVertex*>(alloc_result.pMappedData);
    if (!m_vtx_mapped) {
        ERRORLOG("VKUIRenderer: vertex buffer not persistently mapped");
        return false;
    }
    m_vtx_cursor = 0;
    return true;
}

void VKUIRenderer::Shutdown()
{
    // Ensure all GPU commands using our resources have completed before freeing them.
    // This covers the case where RendererManager deletes us before RendererVulkan::Shutdown().
    if (m_device != VK_NULL_HANDLE && m_vtx_buf != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

    if (m_vtx_buf != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_vtx_buf, m_vtx_alloc);
        m_vtx_buf    = VK_NULL_HANDLE;
        m_vtx_alloc  = VK_NULL_HANDLE;
        m_vtx_mapped = nullptr;
    }
    if (m_slab_sampler    != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_slab_sampler,    nullptr); m_slab_sampler    = VK_NULL_HANDLE; }
    if (m_slab_view       != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_slab_view,     nullptr); m_slab_view       = VK_NULL_HANDLE; }
    if (m_slab_image      != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_slab_image, m_slab_alloc); m_slab_image    = VK_NULL_HANDLE; }
    if (m_minimap_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_minimap_sampler, nullptr); m_minimap_sampler = VK_NULL_HANDLE; }
    if (m_minimap_view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_minimap_view,  nullptr); m_minimap_view    = VK_NULL_HANDLE; }
    if (m_minimap_image   != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_minimap_image, m_minimap_alloc); m_minimap_image = VK_NULL_HANDLE; }
    delete[] m_minimap_cpu_buf;    m_minimap_cpu_buf    = nullptr;
    delete[] m_rt_minimap_cpu_buf; m_rt_minimap_cpu_buf = nullptr;
    m_minimap_cpu_size    = 0;
    m_rt_minimap_cpu_size = 0;
}

/******************************************************************************/
// Per-frame setup (render thread)
/******************************************************************************/

void VKUIRenderer::SetCommandBuffer(VkCommandBuffer cmd, int frame_index)
{
    m_cmd         = cmd;
    m_frame_index = frame_index;
    // Each frame uses its own window of the vertex buffer.
    m_vtx_cursor  = (size_t)frame_index * kVKUIMaxVertices;
}

void VKUIRenderer::FlushPendingInit(VkCommandBuffer cmd, VKStagingRing& staging)
{
    if (m_sprite_atlas) m_sprite_atlas->FlushPendingVK(cmd, staging);
    if (m_font_atlas)   m_font_atlas->FlushPendingVK(cmd, staging);
    flush_slab_texture_upload(cmd, staging);
}

void VKUIRenderer::FlushPendingInit()
{
    // No-op: use FlushPendingInit(cmd, staging) on the render thread instead.
}

/******************************************************************************/
// Viewport
/******************************************************************************/

void VKUIRenderer::SetGameViewport(int x, int y, int w, int h)
{
    m_rt_game_vp_x   = x;
    m_rt_game_vp_y   = y;
    m_rt_game_vp_w   = w;
    m_rt_game_vp_h   = h;
    m_rt_game_vp_set = true;
}

/******************************************************************************/
// IR population (render thread)
/******************************************************************************/

static int IRUILayerToIndex(IRUILayer layer)
{
    switch (layer) {
    case IRUILayer::Back:       return 0;
    case IRUILayer::Front:      return 1;
    case IRUILayer::WorldDepth: return 2;
    case IRUILayer::Overlay:    return 3;
    case IRUILayer::Cursor:     return 4;
    }
    return 1;
}

void VKUIRenderer::PopulateFromIR(const UICommandBuffers& cmds, const FrameState& fs)
{
    ASSERT_RENDER_THREAD();
    if (!cmds.ir_active) return;

    for (int i = 0; i < 5; ++i) { m_rt_quads[i].clear(); m_rt_lines[i].clear(); }
    m_fbo_quads.clear();

    if (cmds.game_vp.set) {
        m_rt_game_vp_x = cmds.game_vp.x; m_rt_game_vp_y = cmds.game_vp.y;
        m_rt_game_vp_w = cmds.game_vp.w; m_rt_game_vp_h = cmds.game_vp.h;
        m_rt_game_vp_set = true;
    } else {
        m_rt_game_vp_set = false;
    }

    const uint8_t* pal = fs.palette;

    // ── Solid boxes ───────────────────────────────────────────────────────────
    for (const auto& cmd : cmds.solid_boxes) {
        const int idx = IRUILayerToIndex(cmd.layer);
        float r = pal[cmd.colour_idx*3+0] / VGA6_MAX;
        float g = pal[cmd.colour_idx*3+1] / VGA6_MAX;
        float b = pal[cmd.colour_idx*3+2] / VGA6_MAX;
        UIQuad q;
        q.x0=(float)cmd.x;       q.y0=(float)cmd.y;
        q.x1=(float)(cmd.x+cmd.w); q.y1=(float)(cmd.y+cmd.h);
        q.u0=0.0f; q.v0=0.0f; q.u1=1.0f; q.v1=1.0f;
        q.r=r; q.g=g; q.b=b; q.a=cmd.alpha;
        q.z=cmd.ndc_z; q.mode=3.0f; q.texture_id=0; q.remap_row=-1;
        m_rt_quads[idx].push_back(q);
    }

    // ── Slab backgrounds ──────────────────────────────────────────────────────
    if (m_slab_image != VK_NULL_HANDLE) {
        for (const auto& cmd : cmds.slab_backgrounds) {
            const int idx = IRUILayerToIndex(cmd.layer);
            const int dim = m_slab_dim;
            if (dim <= 0) continue;
            float u1 = (float)cmd.w / (float)dim;
            float v1 = (float)cmd.h / (float)dim;
            UIQuad qbg;
            qbg.x0=(float)cmd.x; qbg.y0=(float)cmd.y;
            qbg.x1=(float)(cmd.x+cmd.w); qbg.y1=(float)(cmd.y+cmd.h);
            qbg.u0=0.0f; qbg.v0=0.0f; qbg.u1=1.0f; qbg.v1=1.0f;
            qbg.r=0.0f; qbg.g=0.0f; qbg.b=0.0f; qbg.a=1.0f;
            qbg.z=0.48f; qbg.mode=3.0f; qbg.texture_id=0; qbg.remap_row=-1;
            m_rt_quads[idx].push_back(qbg);
            UIQuad qt;
            qt.x0=(float)cmd.x; qt.y0=(float)cmd.y;
            qt.x1=(float)(cmd.x+cmd.w); qt.y1=(float)(cmd.y+cmd.h);
            qt.u0=0.0f; qt.v0=0.0f; qt.u1=u1; qt.v1=v1;
            qt.r=1.0f; qt.g=1.0f; qt.b=1.0f; qt.a=1.0f;
            qt.z=0.49f; qt.mode=10.0f; qt.texture_id=0; qt.remap_row=-1;
            m_rt_quads[idx].push_back(qt);
        }
    }

    // ── Panel sprites ─────────────────────────────────────────────────────────
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
            UIQuad q;
            q.x0=(float)cmd.x; q.y0=(float)cmd.y;
            q.x1=(float)cmd.x+w; q.y1=(float)cmd.y+h;
            q.u0=flip?uv.u1:uv.u0; q.v0=uv.v0; q.u1=flip?uv.u0:uv.u1; q.v1=uv.v1;
            q.r=1.0f; q.g=1.0f; q.b=1.0f; q.a=cmd.alpha;
            q.z=cmd.ndc_z; q.mode=0.0f; q.texture_id=0; q.remap_row=-1;
            m_rt_quads[idx].push_back(q);
        }

        if (m_fade_view != VK_NULL_HANDLE) {
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

        for (const auto& cmd : cmds.sprites_colored) {
            SpriteUV uv;
            if (!m_sprite_atlas->GetUV(cmd.sprite, uv)) continue;
            const int idx = IRUILayerToIndex(cmd.layer);
            float w = (float)((uv.pixel_w * cmd.units_per_px + 8) / 16);
            float h = (float)((uv.pixel_h * cmd.units_per_px + 8) / 16);
            float r = pal[cmd.colour_idx*3+0] / VGA6_MAX;
            float g = pal[cmd.colour_idx*3+1] / VGA6_MAX;
            float b = pal[cmd.colour_idx*3+2] / VGA6_MAX;
            UIQuad q;
            q.x0=(float)cmd.x; q.y0=(float)cmd.y;
            q.x1=(float)cmd.x+w; q.y1=(float)cmd.y+h;
            q.u0=uv.u0; q.v0=uv.v0; q.u1=uv.u1; q.v1=uv.v1;
            q.r=r; q.g=g; q.b=b; q.a=cmd.alpha;
            q.z=cmd.ndc_z; q.mode=20.0f; q.texture_id=0; q.remap_row=-1;
            m_rt_quads[idx].push_back(q);
        }
    }

    // ── Slab selectors ────────────────────────────────────────────────────────
    // Use palette snapshot (pal) — not live m_palette_data — to avoid data races.
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
            UILine ln;
            ln.x1 = (float)cmd.x1 + ndx*t;      ln.y1 = (float)cmd.y1 + ndy*t;
            ln.x2 = (float)cmd.x1 + ndx*t_end;  ln.y2 = (float)cmd.y1 + ndy*t_end;
            ln.r  = pal[pi*3+0]/VGA6_MAX; ln.g = pal[pi*3+1]/VGA6_MAX; ln.b = pal[pi*3+2]/VGA6_MAX;
            ln.a  = 1.0f;
            ln.z  = cmd.z_depth;
            ln.thickness = cmd.thickness;
            m_rt_lines[layer_idx].push_back(ln);
            t = t_end; ++segs;
        }
    }
}

/******************************************************************************/
// Submit methods (game thread)
/******************************************************************************/

static IRUILayer ComputeVKIRLayer(int layer, bool world_depth, bool top_overlay)
{
    if (top_overlay) return IRUILayer::Overlay;
    if (world_depth) return IRUILayer::WorldDepth;
    return (layer == 0) ? IRUILayer::Back : IRUILayer::Front;
}

void VKUIRenderer::SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                     SpriteHandle spr, bool flip_horiz)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas || !m_write_cmds) return;
    IRUISpriteCmd cmd;
    cmd.layer       = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px; cmd.sprite = spr;
    cmd.flags       = flip_horiz ? kIRSpriteFlipHoriz : 0u;
    cmd.alpha       = 1.0f;
    cmd.ndc_z       = m_world_depth_active ? m_world_z : 0.5f;
    m_write_cmds->sprites.Append(cmd);
}

void VKUIRenderer::SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, int remap_row)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas || !m_write_cmds) return;
    IRUISpriteRemapCmd cmd;
    cmd.layer        = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px;
    cmd.sprite       = spr; cmd.remap_row = remap_row; cmd.alpha = 1.0f;
    cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
    m_write_cmds->sprites_remap.Append(cmd);
}

void VKUIRenderer::SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                            SpriteHandle spr, uint8_t color_idx)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas || !m_write_cmds) return;
    IRUISpriteColoredCmd cmd;
    cmd.layer        = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px;
    cmd.sprite       = spr; cmd.colour_idx = color_idx; cmd.alpha = 1.0f;
    cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
    m_write_cmds->sprites_colored.Append(cmd);
}

void VKUIRenderer::SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                      SpriteHandle spr)
{
    if (spr == kInvalidSpriteHandle || !m_sprite_atlas || !m_write_cmds) return;
    IRUISpriteCmd cmd;
    cmd.layer        = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h; cmd.units_per_px = 16;
    cmd.sprite       = spr; cmd.flags = kIRSpriteScaled; cmd.alpha = 1.0f; cmd.ndc_z = 0.5f;
    m_write_cmds->sprites.Append(cmd);
}

void VKUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h,
                                   uint8_t color_idx)
{
    if (!m_write_cmds) return;
    IRUISolidBoxCmd cmd;
    cmd.layer        = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    cmd.colour_idx   = color_idx; cmd.alpha = 1.0f;
    cmd.ndc_z        = m_world_depth_active ? m_world_z : 0.5f;
    m_write_cmds->solid_boxes.Append(cmd);
}

void VKUIRenderer::SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h,
                                        uint8_t color_idx, float alpha)
{
    if (!m_write_cmds) return;
    IRUISolidBoxCmd cmd;
    cmd.layer        = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    cmd.colour_idx   = color_idx; cmd.alpha = alpha; cmd.ndc_z = 0.5f;
    m_write_cmds->solid_boxes.Append(cmd);
}

bool VKUIRenderer::SubmitSlabBackground(int x, int y, int w, int h)
{
    if (m_slab_image == VK_NULL_HANDLE || !m_write_cmds) return false;
    IRUISlabBackgroundCmd cmd;
    cmd.layer = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
    m_write_cmds->slab_backgrounds.Append(cmd);
    return true;
}

void VKUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2,
                                       unsigned char color, float z_depth)
{
    if (!m_write_cmds) return;
    float dx          = (float)(x2 - x1);
    float dy          = (float)(y2 - y1);
    float line_length = std::sqrt(dx * dx + dy * dy);
    if (line_length < 0.001f) return;

    // Mirror GLUIRenderer thickness/step computation exactly.
    float custom_line_box_size = line_box_size / (float)LINE_BOX_SCALE;
    float thickness = custom_line_box_size * units_per_pixel_best / (float)UPP_BASE;
    if (thickness < 1.0f) thickness = 1.0f;
    thickness = thickness + (1.0f - thickness) * (1.0f - hud_scale);
    if (thickness < 1.0f) thickness = 1.0f;

    float step      = (1.0f + 3.0f * (1.0f - hud_scale)) * ((float)STRIPEY_COLORS / (float)units_per_pixel_best);
    if (step < 0.001f) step = 1.0f;
    float band_width = 1.0f / step;

    IRUISlabSelectorCmd cmd;
    cmd.layer       = ComputeVKIRLayer(m_current_layer, m_world_depth_active, m_top_overlay_active);
    cmd.x1          = x1; cmd.y1 = y1; cmd.x2 = x2; cmd.y2 = y2;
    cmd.colour      = color; cmd.z_depth = z_depth;
    cmd.line_length = line_length;
    cmd.thickness   = thickness;
    cmd.band_width  = band_width;
    cmd.step        = step;
    cmd.game_turn   = (uint32_t)(get_gameturn() & (STRIPEY_COLORS - 1));
    m_write_cmds->slab_selectors.Append(cmd);
}

void VKUIRenderer::SubmitFBOQuad(int /*x*/, int /*y*/, int /*w*/, int /*h*/,
                                  GpuTextureHandle /*tex_id*/, float /*clip_radius*/)
{
    // VK FBO quads use VkImageView handles, not GpuTextureHandle integers.
    // Real PiP quads are inserted via BeginPiPSprites/DrawPiPSprites.
}

void VKUIRenderer::BeginPiPSprites()
{
    for (int i = 0; i < 5; ++i) {
        m_pip_quad_wm[i] = (int)m_rt_quads[i].size();
        m_pip_line_wm[i] = (int)m_rt_lines[i].size();
    }
    m_pip_active = true;
}

void VKUIRenderer::DrawPiPSprites(int /*pip_w*/, int /*pip_h*/)
{
    // Trim back PiP sprites — they will be recorded into the PiP FBO separately
    // when the full PiP render pass support is added.
    for (int l = 0; l < 5; ++l) {
        if ((int)m_rt_quads[l].size() > m_pip_quad_wm[l])
            m_rt_quads[l].resize(m_pip_quad_wm[l]);
        if ((int)m_rt_lines[l].size() > m_pip_line_wm[l])
            m_rt_lines[l].resize(m_pip_line_wm[l]);
    }
    m_pip_active = false;
}

/******************************************************************************/
// Minimap
/******************************************************************************/

uint8_t* VKUIRenderer::AcquireMinimapBuffer(int size)
{
    if (size <= 0) return nullptr;
    if (size > m_minimap_cpu_size) {
        delete[] m_minimap_cpu_buf;
        m_minimap_cpu_buf  = new uint8_t[(size_t)size * size];
        m_minimap_cpu_size = size;
    }
    return m_minimap_cpu_buf;
}

void VKUIRenderer::SubmitMinimap(int screen_x, int screen_y, int size)
{
    m_minimap_x       = screen_x;
    m_minimap_y       = screen_y;
    m_minimap_size    = size;
    m_minimap_pending = true;
}

/******************************************************************************/
// Slab texture — game thread writes atomically, render thread reads at flush
/******************************************************************************/

void VKUIRenderer::UpdateSlabTexture(const uint8_t* data, int dim)
{
    m_slab_pending_dim.store(dim,   std::memory_order_release);
    m_slab_pending_data.store(data, std::memory_order_release);
}

void VKUIRenderer::flush_slab_texture_upload(VkCommandBuffer cmd, VKStagingRing& staging)
{
    const uint8_t* data = m_slab_pending_data.load(std::memory_order_acquire);
    const int      dim  = m_slab_pending_dim.load(std::memory_order_acquire);
    if (!data || dim <= 0) return;
    m_slab_pending_data.store(nullptr, std::memory_order_relaxed);

    // Recreate slab image if dim changed.
    if (m_slab_image != VK_NULL_HANDLE && m_slab_dim != dim) {
        if (m_slab_sampler) { vkDestroySampler(m_device, m_slab_sampler, nullptr); m_slab_sampler = VK_NULL_HANDLE; }
        if (m_slab_view)    { vkDestroyImageView(m_device, m_slab_view, nullptr);  m_slab_view    = VK_NULL_HANDLE; }
        vmaDestroyImage(m_allocator, m_slab_image, m_slab_alloc);
        m_slab_image = VK_NULL_HANDLE;
    }

    if (m_slab_image == VK_NULL_HANDLE) {
        VkImageCreateInfo img = {};
        img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType     = VK_IMAGE_TYPE_2D;
        img.format        = VK_FORMAT_R8_UNORM;
        img.extent        = { (uint32_t)dim, (uint32_t)dim, 1 };
        img.mipLevels     = 1; img.arrayLayers = 1;
        img.samples       = VK_SAMPLE_COUNT_1_BIT;
        img.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai = {}; ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(m_allocator, &img, &ai, &m_slab_image, &m_slab_alloc, nullptr) != VK_SUCCESS) return;

        VkImageViewCreateInfo vi = {};
        vi.sType  = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image  = m_slab_image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &vi, nullptr, &m_slab_view) != VK_SUCCESS) return;

        VkSamplerCreateInfo si = {};
        si.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter  = VK_FILTER_NEAREST; si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        if (vkCreateSampler(m_device, &si, nullptr, &m_slab_sampler) != VK_SUCCESS) return;

        m_slab_dim = dim;
    }

    VKStagingAlloc sa;
    if (!staging.Alloc(data, (VkDeviceSize)dim*dim, 1, sa)) {
        ERRORLOG("VKUIRenderer: staging ring exhausted for slab texture"); return;
    }

    VkImageMemoryBarrier b = {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = m_slab_image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);

    staging.CmdCopyToImage(cmd, sa.offset, sa.buffer, m_slab_image, (uint32_t)dim, (uint32_t)dim);

    b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);
}

/******************************************************************************/
// FlipBuffers (game thread → render thread)
/******************************************************************************/

void VKUIRenderer::FlipBuffers()
{
    std::swap(m_minimap_cpu_buf,  m_rt_minimap_cpu_buf);
    std::swap(m_minimap_cpu_size, m_rt_minimap_cpu_size);
    m_rt_minimap_x       = m_minimap_x;
    m_rt_minimap_y       = m_minimap_y;
    m_rt_minimap_size    = m_minimap_size;
    m_rt_minimap_pending = m_minimap_pending;
    m_minimap_pending    = false;
}

void VKUIRenderer::Clear()
{
    for (int i = 0; i < 5; ++i) { m_rt_quads[i].clear(); m_rt_lines[i].clear(); }
    m_fbo_quads.clear();
}

/******************************************************************************/
// Vertex expansion helpers
/******************************************************************************/

void VKUIRenderer::expand_quad_to_vertices(const UIQuad& q, UIVertex* v) const
{
    v[0] = {q.x0, q.y0, q.u0, q.v0, q.r, q.g, q.b, q.a, q.z, q.mode};
    v[1] = {q.x0, q.y1, q.u0, q.v1, q.r, q.g, q.b, q.a, q.z, q.mode};
    v[2] = {q.x1, q.y0, q.u1, q.v0, q.r, q.g, q.b, q.a, q.z, q.mode};
    v[3] = {q.x1, q.y0, q.u1, q.v0, q.r, q.g, q.b, q.a, q.z, q.mode};
    v[4] = {q.x0, q.y1, q.u0, q.v1, q.r, q.g, q.b, q.a, q.z, q.mode};
    v[5] = {q.x1, q.y1, q.u1, q.v1, q.r, q.g, q.b, q.a, q.z, q.mode};
}

void VKUIRenderer::expand_line_to_vertices(const UILine& l, UIVertex* v) const
{
    Vec2f dir(l.x2 - l.x1, l.y2 - l.y1);
    float len = dir.length();
    if (len < 0.001f) return;
    Vec2f perp = (dir / len).perp() * (l.thickness * 0.5f);
    v[0] = {l.x1+perp.x, l.y1+perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
    v[1] = {l.x1-perp.x, l.y1-perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
    v[2] = {l.x2+perp.x, l.y2+perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
    v[3] = {l.x2+perp.x, l.y2+perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
    v[4] = {l.x1-perp.x, l.y1-perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
    v[5] = {l.x2-perp.x, l.y2-perp.y, 0.f, 0.f, l.r, l.g, l.b, l.a, l.z, 2.0f};
}

/******************************************************************************/
// push_constants helper
/******************************************************************************/

void VKUIRenderer::setup_push_constants_ui(float remap_row, float z_ndc,
                                            float clip_x0, float clip_y0,
                                            float clip_x1, float clip_y1,
                                            float clip_radius)
{
    VKPushConstants pc = {};
    pc.screen_w      = (float)m_screen_w;
    pc.screen_h      = (float)m_screen_h;
    pc.z_ndc         = z_ndc;
    pc.alpha         = 1.0f;
    pc.clip_rect[0]  = clip_x0; pc.clip_rect[1] = clip_y0;
    pc.clip_rect[2]  = clip_x1; pc.clip_rect[3] = clip_y1;
    pc.clip_radius   = clip_radius;
    pc.clip_screen_h = (float)m_screen_h;
    pc.remap_row     = remap_row;
    vkCmdPushConstants(m_cmd, m_pipelines->GetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(VKPushConstants), &pc);
}

/******************************************************************************/
// record_draw / upload_vertices (helpers used by flush_layer)
/******************************************************************************/

bool VKUIRenderer::upload_vertices(const UIVertex* verts, size_t count,
                                   VkBuffer& out_buf, VkDeviceSize& out_off)
{
    size_t frame_start  = (size_t)m_frame_index * kVKUIMaxVertices;
    size_t frame_end    = frame_start + kVKUIMaxVertices;
    if (m_vtx_cursor + count > frame_end) {
        WARNLOG("VKUIRenderer: vertex buffer exhausted (cursor=%zu end=%zu need=%zu)",
                m_vtx_cursor, frame_end, count);
        return false;
    }
    std::memcpy(m_vtx_mapped + m_vtx_cursor, verts, count * sizeof(UIVertex));
    out_buf = m_vtx_buf;
    out_off = (VkDeviceSize)(m_vtx_cursor * sizeof(UIVertex));
    m_vtx_cursor += count;
    return true;
}

void VKUIRenderer::record_draw(VkPipeline pipeline, VkDescriptorSet desc_set,
                                VkBuffer vtx_buf, VkDeviceSize vtx_off,
                                uint32_t vtx_count)
{
    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelines->GetLayout(), 0, 1, &desc_set, 0, nullptr);
    vkCmdBindVertexBuffers(m_cmd, 0, 1, &vtx_buf, &vtx_off);
    vkCmdDraw(m_cmd, vtx_count, 1, 0, 0);
}

/******************************************************************************/
// flush_layer — batch-draw all quads and lines for one layer
/******************************************************************************/

enum VKUIPass { VKUI_SLAB=0, VKUI_SOLID, VKUI_SPRITE, VKUI_COLORED, VKUI_FONT, VKUI_REMAP };

static VKUIPass classify_quad_mode(float mode) {
    if (mode >= 29.5f) return VKUI_REMAP;
    if (mode >= 19.5f) return VKUI_COLORED;
    if (mode >=  9.5f) return VKUI_SLAB;
    if (mode >=  2.5f) return VKUI_SOLID;
    if (mode >=  0.5f) return VKUI_FONT;
    return VKUI_SPRITE;
}

static VKPassType vkpass_for_ui(VKUIPass p) {
    switch (p) {
    case VKUI_SOLID:   return VKPassType::UI_Solid;
    case VKUI_SPRITE:  return VKPassType::UI_Sprite;
    case VKUI_SLAB:    return VKPassType::UI_Sprite;  // REPEAT sampler; same pipeline
    case VKUI_COLORED: return VKPassType::UI_Colored;
    case VKUI_FONT:    return VKPassType::UI_Font;
    case VKUI_REMAP:   return VKPassType::UI_Remap;
    }
    return VKPassType::UI_Sprite;
}

/** Write descriptors for a batch and return the allocated set (VK_NULL_HANDLE on failure). */
VkDescriptorSet VKUIRenderer::alloc_and_bind_descriptors(int pass, int remap_row)
{
    VkDescriptorSet ds = m_descriptors->Alloc();
    if (ds == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    (void)remap_row; // remap row is sent via push constants, not a descriptor

    switch (pass) {
    case VKUI_SPRITE:
        if (m_sprite_atlas) {
            m_descriptors->UpdateCombinedImageSampler(ds, 0, m_sprite_atlas->GetView(), m_sprite_atlas->GetSampler());
            if (m_palette_view)
                m_descriptors->UpdateCombinedImageSampler(ds, 1, m_palette_view, m_palette_sampler);
        }
        break;
    case VKUI_SLAB:
        if (m_slab_view != VK_NULL_HANDLE) {
            m_descriptors->UpdateCombinedImageSampler(ds, 0, m_slab_view, m_slab_sampler);
            if (m_palette_view)
                m_descriptors->UpdateCombinedImageSampler(ds, 1, m_palette_view, m_palette_sampler);
        }
        break;
    case VKUI_COLORED:
        if (m_sprite_atlas)
            m_descriptors->UpdateCombinedImageSampler(ds, 0, m_sprite_atlas->GetView(), m_sprite_atlas->GetSampler());
        break;
    case VKUI_FONT:
        if (m_font_atlas)
            m_descriptors->UpdateCombinedImageSampler(ds, 0, m_font_atlas->GetView(), m_font_atlas->GetSampler());
        break;
    case VKUI_REMAP:
        if (m_sprite_atlas && m_fade_view != VK_NULL_HANDLE) {
            m_descriptors->UpdateCombinedImageSampler(ds, 0, m_sprite_atlas->GetView(), m_sprite_atlas->GetSampler());
            if (m_palette_view)
                m_descriptors->UpdateCombinedImageSampler(ds, 1, m_palette_view, m_palette_sampler);
            m_descriptors->UpdateCombinedImageSampler(ds, 2, m_fade_view, m_fade_sampler);
        }
        break;
    default:
        break;
    }
    return ds;
}

void VKUIRenderer::flush_layer(int layer, bool depth_test)
{
    auto& quads = m_rt_quads[layer];
    auto& lines = m_rt_lines[layer];
    if (quads.empty() && lines.empty()) return;

    // Scissor the WorldDepth layer (layer 2) to the game viewport.
    if (depth_test && m_rt_game_vp_set) {
        VkRect2D scissor = {};
        scissor.offset = { m_rt_game_vp_x, m_rt_game_vp_y };
        scissor.extent = { (uint32_t)m_rt_game_vp_w, (uint32_t)m_rt_game_vp_h };
        vkCmdSetScissor(m_cmd, 0, 1, &scissor);
    }

    // Temporary per-batch vertex scratch buffer on the stack / small heap.
    // Maximum batch size = all quads in the layer × 6 verts.
    // We write directly into the persistent vertex buffer, batching by pass type.
    if (!quads.empty()) {
        VKUIPass  cur_pass       = classify_quad_mode(quads.front().mode);
        int       cur_remap      = quads.front().remap_row;
        size_t    batch_vtx_base = m_vtx_cursor;  // start of this batch in the buffer
        uint32_t  batch_verts    = 0;

        auto flush_batch = [&]() {
            if (batch_verts == 0) return;
            VkPipeline pipe = m_pipelines->GetPipeline(vkpass_for_ui(cur_pass));
            if (pipe == VK_NULL_HANDLE) { m_vtx_cursor = batch_vtx_base; batch_verts = 0; return; }
            VkDescriptorSet ds = alloc_and_bind_descriptors((int)cur_pass, cur_remap);
            if (ds == VK_NULL_HANDLE) { m_vtx_cursor = batch_vtx_base; batch_verts = 0; return; }
            setup_push_constants_ui((float)cur_remap, 0.5f,
                                    0.0f, 0.0f, (float)m_screen_w, (float)m_screen_h, -1.0f);
            VkDeviceSize vtx_off = (VkDeviceSize)(batch_vtx_base * sizeof(UIVertex));
            record_draw(pipe, ds, m_vtx_buf, vtx_off, batch_verts);
            batch_verts    = 0;
            batch_vtx_base = m_vtx_cursor;
        };

        for (const auto& q : quads) {
            VKUIPass pass         = classify_quad_mode(q.mode);
            bool     remap_change = (pass == VKUI_REMAP && q.remap_row != cur_remap);
            if (pass != cur_pass || remap_change) {
                flush_batch();
                cur_pass  = pass;
                cur_remap = q.remap_row;
            }
            size_t frame_end = (size_t)m_frame_index * kVKUIMaxVertices + kVKUIMaxVertices;
            if (m_vtx_cursor + 6 > frame_end) {
                WARNLOG("VKUIRenderer: vertex buffer full in flush_layer(%d)", layer);
                break;
            }
            expand_quad_to_vertices(q, m_vtx_mapped + m_vtx_cursor);
            m_vtx_cursor += 6;
            batch_verts  += 6;
        }
        flush_batch();
    }

    // Lines (solid pass only — no texture needed).
    if (!lines.empty()) {
        VkPipeline pipe = m_pipelines->GetPipeline(VKPassType::UI_Solid);
        if (pipe != VK_NULL_HANDLE) {
            VkDescriptorSet ds         = m_descriptors->Alloc();
            size_t          line_base  = m_vtx_cursor;
            uint32_t        line_verts = 0;
            size_t          frame_end  = (size_t)m_frame_index * kVKUIMaxVertices + kVKUIMaxVertices;
            for (const auto& l : lines) {
                if (m_vtx_cursor + 6 > frame_end) break;
                expand_line_to_vertices(l, m_vtx_mapped + m_vtx_cursor);
                m_vtx_cursor += 6;
                line_verts   += 6;
            }
            if (line_verts > 0 && ds != VK_NULL_HANDLE) {
                setup_push_constants_ui(-1.0f, 0.5f, 0.0f, 0.0f,
                                        (float)m_screen_w, (float)m_screen_h, -1.0f);
                VkDeviceSize vtx_off = (VkDeviceSize)(line_base * sizeof(UIVertex));
                record_draw(pipe, ds, m_vtx_buf, vtx_off, line_verts);
            }
        }
    }

    // Restore full-screen scissor when leaving a depth-tested layer.
    if (depth_test && m_rt_game_vp_set) {
        VkRect2D full = { {0, 0}, {(uint32_t)m_screen_w, (uint32_t)m_screen_h} };
        vkCmdSetScissor(m_cmd, 0, 1, &full);
    }

    quads.clear();
    lines.clear();
}

/******************************************************************************/
// Draw* entry points
/******************************************************************************/

void VKUIRenderer::DrawBack()
{
    ASSERT_RENDER_THREAD();
    if (m_cmd == VK_NULL_HANDLE) return;
    flush_layer(0, false);
}

void VKUIRenderer::DrawFrontBase()
{
    ASSERT_RENDER_THREAD();
    if (m_cmd == VK_NULL_HANDLE) return;
    flush_layer(1, false);
    // Minimap upload + draw — deferred to Phase 6 when staging ring is wired in.
    if (m_rt_minimap_pending) m_rt_minimap_pending = false;
}

void VKUIRenderer::DrawFrontOverlay()
{
    ASSERT_RENDER_THREAD();
    if (m_cmd == VK_NULL_HANDLE) return;
    flush_layer(2, true);   // WorldDepth — depth-tested, scissored
    flush_layer(3, false);  // Overlay
    flush_layer(4, false);  // Cursor
}

void VKUIRenderer::DrawFront()
{
    DrawFrontBase();
    DrawFrontOverlay();
}

void VKUIRenderer::DrawWorldSprites()
{
    ASSERT_RENDER_THREAD();
    if (m_cmd == VK_NULL_HANDLE) return;
    flush_layer(2, true);
}

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
