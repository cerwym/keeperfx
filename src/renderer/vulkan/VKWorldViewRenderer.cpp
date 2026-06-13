/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKWorldViewRenderer.cpp
 *     Vulkan world-geometry renderer implementation.
 * @par Purpose:
 *     Full IWorldViewRenderer implementation for the Vulkan backend.
 *     Mirrors GLWorldViewRenderer feature-for-feature; replaces GL state
 *     machine calls with Vulkan command buffer recording.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKWorldViewRenderer.h"

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include "renderer/TileAtlasPacker.h"
#include "renderer/RendererThread.h"
#include "renderer/VecMath.h"
#include "engine_buckets.h"    // QKinds enum, BasicQ, BucketKind* structs, buckets[]
#include "engine_textures.h"   // TEXTURE_BLOCKS_COUNT, block_count_per_row
#include "engine_render.h"     // display_drawlist(), display_fast_drawlist(), BUCKETS_COUNT
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG / WARNLOG
#include "bflib_sprite.h"
#include "renderer/RendererManager.h"  // WorldViewRenderer_GetCurrentSpriteOwner etc.
#include "globals.h"           // game.lish.subtile_lightness, MAX_SUBTILES_*
#include "renderer/RendererSettings.h"  // g_renderer_settings

#include <cassert>
#include <cstring>
#include <chrono>
#include <algorithm>
#include "post_inc.h"

/******************************************************************************/
// Scratch buffers (static, render-thread only)
/******************************************************************************/

/** Keeper-sprite decode scratch. Stride is always k_kspr_decode_dim. */
static uint8_t s_vk_kspr_decode_buf[VKWorldViewRenderer::k_kspr_decode_dim *
                                     VKWorldViewRenderer::k_kspr_decode_dim];

/** Decode keeper-sprite RLE into a stride-k_kspr_decode_dim palette-index buffer.
 *  Identical algorithm to GLWorldViewRenderer's file-static decode_keeper_rle(). */
static void decode_keeper_rle_vk(uint8_t* dst, const uint8_t* data, int w, int h)
{
    if (!data || w <= 0 || h <= 0) return;

    const int stride = VKWorldViewRenderer::k_kspr_decode_dim;
    for (int y = 0; y < h; ++y)
        memset(dst + y * stride, 0, (size_t)stride);

    const signed char* sp     = reinterpret_cast<const signed char*>(data);
    const signed char* sp_end = sp + (ptrdiff_t)w * h * 3 + h;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + y * stride;
        int x = 0;
        while (true) {
            if (sp >= sp_end) {
                WARNLOG("decode_keeper_rle_vk: ran past data end at row %d", y);
                return;
            }
            signed char cmd = *sp++;
            if (cmd == 0) break;
            if (cmd < 0) {
                x += (int)(-cmd);
            } else {
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
                    if (sp >= sp_end) { WARNLOG("decode_keeper_rle_vk: pixel past end"); return; }
                    if (x < w) row[x] = (uint8_t)(*sp);
                    ++sp; ++x;
                }
            }
        }
    }
}

/******************************************************************************/
// Init / Shutdown
/******************************************************************************/

bool VKWorldViewRenderer::Init(
    VkDevice           device,
    VmaAllocator       allocator,
    VKPipelineCache*   pipelines,
    VKDescriptorLayout* desc_layout,
    VKStagingRing*     staging,
    VKTileAtlas*       tile_atlas,
    VkImageView        palette_view,
    VkSampler          palette_sampler,
    VkImageView        fade_view,
    VkSampler          fade_sampler)
{
    if (m_initialized) return true;

    m_device          = device;
    m_allocator       = allocator;
    m_pipelines       = pipelines;
    m_desc_layout     = desc_layout;
    m_staging         = staging;
    m_tile_atlas      = tile_atlas;
    m_palette_view    = palette_view;
    m_palette_sampler = palette_sampler;
    m_fade_view       = fade_view;
    m_fade_sampler    = fade_sampler;

    // ── World vertex buffer (double-buffered) ─────────────────────────────────
    // Two frames × k_max_verts × sizeof(WorldVertex), CPU_TO_GPU persistent map.
    const VkDeviceSize vtx_bytes = (VkDeviceSize)k_max_verts * sizeof(WorldVertex) * 2;
    {
        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = vtx_bytes;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai = {};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ainfo = {};
        if (vmaCreateBuffer(m_allocator, &bi, &ai, &m_vtx_buf, &m_vtx_alloc, &ainfo) != VK_SUCCESS) {
            ERRORLOG("VKWorldViewRenderer: vertex buffer alloc failed");
            return false;
        }
        m_vtx_mapped = static_cast<WorldVertex*>(ainfo.pMappedData);
        // GT write buffer starts at window 0 (frame 0).
        m_verts    = m_vtx_mapped;
        m_rt_verts = m_vtx_mapped + k_max_verts; // render-thread window
    }

    // ── Flat-poly vertex buffer ───────────────────────────────────────────────
    const VkDeviceSize fp_bytes = (VkDeviceSize)k_max_flatpoly_verts * sizeof(FlatPolyVertex) * 2;
    {
        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = fp_bytes;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai = {};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ainfo = {};
        if (vmaCreateBuffer(m_allocator, &bi, &ai, &m_fp_buf, &m_fp_alloc, &ainfo) != VK_SUCCESS) {
            ERRORLOG("VKWorldViewRenderer: flat-poly vertex buffer alloc failed");
            return false;
        }
        m_fp_mapped = static_cast<FlatPolyVertex*>(ainfo.pMappedData);
    }

    // ── Transient vertex buffer for sprite / shadow quads ────────────────────
    // Per-sprite quads use a 5-float vertex (x,y,u,v,layer) — different format
    // from WorldVertex.  We need a separate VERTEX_BUFFER_BIT buffer.
    {
        const VkDeviceSize transient_bytes =
            (VkDeviceSize)k_max_transient_verts * 5 * sizeof(float) * 2;
        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = transient_bytes;
        bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo ai = {};
        ai.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo ainfo = {};
        if (vmaCreateBuffer(m_allocator, &bi, &ai, &m_transient_buf, &m_transient_alloc, &ainfo) != VK_SUCCESS) {
            ERRORLOG("VKWorldViewRenderer: transient vertex buffer alloc failed");
            return false;
        }
        m_transient_mapped = static_cast<uint8_t*>(ainfo.pMappedData);
        m_transient_cursor = 0;
    }

    // ── Shadow silhouette image (R8_UNORM 256×256, reused per shadow) ─────────
    {
        VkImageCreateInfo img = {};
        img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType     = VK_IMAGE_TYPE_2D;
        img.format        = VK_FORMAT_R8_UNORM;
        img.extent        = { 256, 256, 1 };
        img.mipLevels     = 1;
        img.arrayLayers   = 1;
        img.samples       = VK_SAMPLE_COUNT_1_BIT;
        img.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo ai = {}; ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(m_allocator, &img, &ai, &m_shadow_img, &m_shadow_alloc, nullptr) != VK_SUCCESS) {
            ERRORLOG("VKWorldViewRenderer: shadow image alloc failed");
            return false;
        }
        VkImageViewCreateInfo vi = {};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = m_shadow_img;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(m_device, &vi, nullptr, &m_shadow_view);
        VkSamplerCreateInfo si = {};
        si.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter  = VK_FILTER_NEAREST;
        si.minFilter  = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(m_device, &si, nullptr, &m_shadow_sampler);
    }

    // ── Keeper sprite atlas (allocated lazily on first use) ───────────────────
    // init_kspr_atlas() is deferred to FlushPendingInit() / first sprite draw.

    // ── CLUT image (256 × k_clut_rows RGBA8_UNORM) ───────────────────────────
    {
        VkImageCreateInfo img = {};
        img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType     = VK_IMAGE_TYPE_2D;
        img.format        = VK_FORMAT_R8G8B8A8_UNORM;
        img.extent        = { 256, (uint32_t)k_clut_rows, 1 };
        img.mipLevels     = 1;
        img.arrayLayers   = 1;
        img.samples       = VK_SAMPLE_COUNT_1_BIT;
        img.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo ai = {}; ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(m_allocator, &img, &ai, &m_clut_image, &m_clut_alloc, nullptr) != VK_SUCCESS) {
            ERRORLOG("VKWorldViewRenderer: CLUT image alloc failed");
            return false;
        }
        VkImageViewCreateInfo vi = {};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = m_clut_image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(m_device, &vi, nullptr, &m_clut_view);
        VkSamplerCreateInfo si = {};
        si.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter  = VK_FILTER_NEAREST;
        si.minFilter  = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        vkCreateSampler(m_device, &si, nullptr, &m_clut_sampler);
    }

    m_vert_count     = 0;
    m_cmd_vert_start = 0;
    m_rt_vert_count  = 0;
    m_initialized    = true;
    SYNCLOG("VKWorldViewRenderer: initialised");
    return true;
}

void VKWorldViewRenderer::Shutdown()
{
    if (!m_initialized) return;
    m_initialized      = false;
    m_shadow_img_ready = false;
    m_clut_img_ready   = false;

    // Ensure all GPU commands using our resources have completed before freeing them.
    // This covers the case where RendererManager deletes us before RendererVulkan::Shutdown().
    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

    free_kspr_atlas();

    if (m_clut_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_clut_sampler, nullptr); m_clut_sampler = VK_NULL_HANDLE; }
    if (m_clut_view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_clut_view, nullptr);  m_clut_view    = VK_NULL_HANDLE; }
    if (m_clut_image   != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_clut_image, m_clut_alloc); m_clut_image = VK_NULL_HANDLE; }

    if (m_shadow_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_shadow_sampler, nullptr); m_shadow_sampler = VK_NULL_HANDLE; }
    if (m_shadow_view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_shadow_view, nullptr);  m_shadow_view    = VK_NULL_HANDLE; }
    if (m_shadow_img     != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_shadow_img, m_shadow_alloc); m_shadow_img = VK_NULL_HANDLE; }

    if (m_fp_buf  != VK_NULL_HANDLE) { vmaDestroyBuffer(m_allocator, m_fp_buf,  m_fp_alloc);  m_fp_buf  = VK_NULL_HANDLE; }
    if (m_transient_buf != VK_NULL_HANDLE) { vmaDestroyBuffer(m_allocator, m_transient_buf, m_transient_alloc); m_transient_buf = VK_NULL_HANDLE; }
    if (m_vtx_buf != VK_NULL_HANDLE) { vmaDestroyBuffer(m_allocator, m_vtx_buf, m_vtx_alloc); m_vtx_buf = VK_NULL_HANDLE; }

    m_verts    = nullptr;
    m_rt_verts = nullptr;
}

/******************************************************************************/
// Keeper sprite atlas
/******************************************************************************/

bool VKWorldViewRenderer::init_kspr_atlas()
{
    if (m_kspr_image != VK_NULL_HANDLE) return true;  // already created

    VkImageCreateInfo img = {};
    img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType     = VK_IMAGE_TYPE_2D;
    img.format        = VK_FORMAT_R8_UNORM;
    img.extent        = { (uint32_t)k_kspr_decode_dim, (uint32_t)k_kspr_decode_dim, 1 };
    img.mipLevels     = 1;
    img.arrayLayers   = (uint32_t)k_kspr_atlas_layers;
    img.samples       = VK_SAMPLE_COUNT_1_BIT;
    img.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo ai = {}; ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(m_allocator, &img, &ai, &m_kspr_image, &m_kspr_alloc, nullptr) != VK_SUCCESS) {
        ERRORLOG("VKWorldViewRenderer: keeper-sprite atlas alloc failed");
        return false;
    }
    VkImageViewCreateInfo vi = {};
    vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image    = m_kspr_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format   = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)k_kspr_atlas_layers };
    if (vkCreateImageView(m_device, &vi, nullptr, &m_kspr_view) != VK_SUCCESS) {
        ERRORLOG("VKWorldViewRenderer: keeper-sprite atlas view creation failed");
        return false;
    }
    VkSamplerCreateInfo si = {};
    si.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter  = VK_FILTER_NEAREST;
    si.minFilter  = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(m_device, &si, nullptr, &m_kspr_sampler) != VK_SUCCESS) {
        ERRORLOG("VKWorldViewRenderer: keeper-sprite atlas sampler creation failed");
        return false;
    }

    // Transition to SHADER_READ_ONLY_OPTIMAL (all layers undefined → readable).
    // The actual image data will be uploaded per-sprite on first use.
    // We need a command buffer for this; it will be done on the next FlushPendingInit().
    SYNCLOG("VKWorldViewRenderer: keeper-sprite decode atlas ready (%d layers, %dx%d R8)",
            k_kspr_atlas_layers, k_kspr_decode_dim, k_kspr_decode_dim);
    return true;
}

void VKWorldViewRenderer::free_kspr_atlas()
{
    if (m_kspr_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_kspr_sampler, nullptr); m_kspr_sampler = VK_NULL_HANDLE; }
    if (m_kspr_view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_kspr_view, nullptr);  m_kspr_view    = VK_NULL_HANDLE; }
    if (m_kspr_image   != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_kspr_image, m_kspr_alloc); m_kspr_image = VK_NULL_HANDLE; }
    m_kspr_atlas_used = 0;
    m_kspr_atlas_map.clear();
}

/******************************************************************************/
// CLUT helpers
/******************************************************************************/

int VKWorldViewRenderer::alloc_clut_row(const uint8_t* remap)
{
    if (!remap) return 0;  // identity row
    auto it = m_kspr_clut_map.find(remap);
    if (it != m_kspr_clut_map.end()) return it->second;
    if (m_kspr_clut_used >= k_clut_rows) return 0;  // full — fall back to identity
    int row = m_kspr_clut_used++;
    m_kspr_clut_map[remap] = row;
    m_clut_dirty = true;
    return row;
}

void VKWorldViewRenderer::ensure_clut_valid(VkCommandBuffer cmd)
{
    if (!m_palette_data) return;

    // Check if the palette has changed since last upload.
    const bool palette_changed = (memcmp(m_clut_palette_snap, m_rt_palette, 768) != 0);
    if (!palette_changed && !m_clut_dirty) return;

    memcpy(m_clut_palette_snap, m_rt_palette, 768);

    // Rebuild all allocated CLUT rows and upload via staging ring.
    // Row 0 = identity: palette[i] for all i.
    static uint8_t s_clut_buf[256 * VKWorldViewRenderer::k_clut_rows * 4];
    memset(s_clut_buf, 0, sizeof(s_clut_buf));

    // Row 0: identity
    const uint8_t* pal = m_rt_palette;
    for (int i = 0; i < 256; ++i) {
        s_clut_buf[i * 4 + 0] = pal[i * 3 + 0];
        s_clut_buf[i * 4 + 1] = pal[i * 3 + 1];
        s_clut_buf[i * 4 + 2] = pal[i * 3 + 2];
        s_clut_buf[i * 4 + 3] = (i != 0) ? 255 : 0;
    }

    // Remapped rows: palette[remap[i]]
    for (auto& kv : m_kspr_clut_map) {
        const uint8_t* remap = kv.first;
        int row              = kv.second;
        uint8_t* dst         = s_clut_buf + row * 256 * 4;
        for (int i = 0; i < 256; ++i) {
            uint8_t ri = remap[i];
            dst[i * 4 + 0] = pal[ri * 3 + 0];
            dst[i * 4 + 1] = pal[ri * 3 + 1];
            dst[i * 4 + 2] = pal[ri * 3 + 2];
            dst[i * 4 + 3] = (i != 0) ? 255 : 0;
        }
    }

    const int   rows  = m_kspr_clut_used;
    const size_t sz   = (size_t)256 * rows * 4;

    // Transition to TRANSFER_DST
    VkImageMemoryBarrier b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = m_clut_image;
    b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);

    VKStagingAlloc sa;
    if (m_staging->Alloc(s_clut_buf, sz, 4, sa)) {
        VkBufferImageCopy region = {};
        region.bufferOffset                    = sa.offset;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { 0, 0, 0 };
        region.imageExtent                     = { 256, (uint32_t)rows, 1 };
        vkCmdCopyBufferToImage(cmd, sa.buffer, m_clut_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b);

    m_clut_dirty = false;
}

/******************************************************************************/
// SetCommandBuffer / FlushPendingInit
/******************************************************************************/

void VKWorldViewRenderer::SetCommandBuffer(VkCommandBuffer cmd, int frame_index)
{
    ASSERT_RENDER_THREAD();
    m_cmd            = cmd;
    m_rt_frame_index = frame_index;
}

void VKWorldViewRenderer::FlushPendingInit(VkCommandBuffer cmd)
{
    ASSERT_RENDER_THREAD();
    if (!m_initialized) return;

    // Init keeper-sprite atlas lazily on render thread (VkImage creation is safe here).
    if (m_kspr_image == VK_NULL_HANDLE) init_kspr_atlas();

    // Tile atlas upload (if any variation was dirtied by game thread).
    if (m_tile_atlas) m_tile_atlas->FlushPendingVK(cmd, *m_staging);

    // Transition shadow image to SHADER_READ_ONLY_OPTIMAL on first use.
    if (!m_shadow_img_ready && m_shadow_img != VK_NULL_HANDLE) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_shadow_img;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        m_shadow_img_ready = true;
    }
    // Transition CLUT image from UNDEFINED → SHADER_READ_ONLY_OPTIMAL on first use.
    if (!m_clut_img_ready && m_clut_image != VK_NULL_HANDLE) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_clut_image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
        // Also transition kspr atlas if it was just created.
        if (m_kspr_image != VK_NULL_HANDLE) {
            b.image = m_kspr_image;
            b.subresourceRange.layerCount = (uint32_t)k_kspr_atlas_layers;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }
        m_clut_img_ready = true;
        m_clut_dirty = true;  // force upload on first real frame
    }
}

/******************************************************************************/
// IWorldViewRenderer — game-thread methods
/******************************************************************************/

void VKWorldViewRenderer::BeginWorldPass(uint8_t* framebuf, int pitch,
                                          int w, int h, int vp_x, int vp_y)
{
    ASSERT_GAME_THREAD();
    m_screen_w        = w;
    m_screen_h        = h;
    m_vp_x            = vp_x;
    m_vp_y            = vp_y;
    m_framebuf        = framebuf;
    m_pitch           = pitch;
    m_current_bucket  = 0;
    m_world_pass_active = true;

    gpu_flush();
    m_cmd_vert_start = m_vert_count;

    if (!m_initialized) return;

    // Lazy-init tile atlas on game thread if engine data is available.
    if (m_tile_atlas && !m_tile_atlas->IsInitialized() && block_mem != nullptr)
        m_tile_atlas->Init();

    if (w > 0)  vec_window_width  = (long)w;
    if (h > 0)  vec_window_height = (long)h;
}

void VKWorldViewRenderer::DrawIsometricView()
{
    ASSERT_GAME_THREAD();
    if (!m_initialized) return;

    display_drawlist();
    gpu_flush();
}

void VKWorldViewRenderer::DrawFrontView(struct Camera* cam)
{
    ASSERT_GAME_THREAD();
    if (!m_initialized) return;

    display_fast_drawlist(cam);
    gpu_flush();
}

/******************************************************************************/
// Geometry helpers (game thread)
/******************************************************************************/

bool VKWorldViewRenderer::append_triangle(int tile_id,
                                           const struct PolyPoint* p0,
                                           const struct PolyPoint* p1,
                                           const struct PolyPoint* p2,
                                           int32_t cam_z0, int32_t cam_z1, int32_t cam_z2,
                                           int32_t wx0, int32_t wy0, int32_t wz0,
                                           int32_t wx1, int32_t wy1, int32_t wz1,
                                           int32_t wx2, int32_t wy2, int32_t wz2)
{
    const int variation  = tile_id / TEXTURE_BLOCKS_COUNT;
    const int tile_local = tile_id % TEXTURE_BLOCKS_COUNT;

    if (m_vert_count + 3 > k_max_verts) {
        gpu_flush();
        if (m_vert_count + 3 > k_max_verts) return false;
    }

    float u0f, v0f, u1f, v1f;
    TileAtlasPacker::GetTileUV(tile_local, &u0f, &v0f, &u1f, &v1f);

    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;
    const float layer = (float)variation;

    const struct PolyPoint* pts[3] = { p0, p1, p2 };
    const int32_t cam_z[3] = { cam_z0, cam_z1, cam_z2 };
    const int32_t world_x[3] = { wx0, wx1, wx2 };
    const int32_t world_y[3] = { wy0, wy1, wy2 };
    const int32_t world_z[3] = { wz0, wz1, wz2 };
    WorldVertex* v = &m_verts[m_vert_count];
    for (int i = 0; i < 3; i++) {
        v[i].x    = (float)(pts[i]->X) / (float)m_screen_w * 2.0f - 1.0f;
        v[i].y    = 1.0f - (float)(pts[i]->Y) / (float)m_screen_h * 2.0f;
        v[i].z    = z_ndc;
        v[i].u    = u0f + ((float)(pts[i]->U >> 16) / 32.0f) * (u1f - u0f);
        v[i].v    = v0f + ((float)(pts[i]->V >> 16) / 32.0f) * (v1f - v0f);
        v[i].shade = (float)(pts[i]->S >> 16) / 32.0f;
        v[i].stl_x = 0.0f;
        v[i].stl_y = 0.0f;
        v[i].camera_z   = (cam_z[i] > 0) ? (float)cam_z[i] : 1.0f;
        v[i].atlas_layer = layer;
        v[i].wx = (float)world_x[i];
        v[i].wy = (float)world_y[i];
        v[i].wz = (float)world_z[i];
    }
    m_vert_count += 3;
    return true;
}

bool VKWorldViewRenderer::append_triangle_compact(
    int sx0, int sy0, int u0, int v0, int shade0,
    int sx1, int sy1, int u1, int v1, int shade1,
    int sx2, int sy2, int u2, int v2, int shade2)
{
    if (m_vert_count + 3 > k_max_verts) {
        gpu_flush();
        if (m_vert_count + 3 > k_max_verts) return false;
    }

    auto compact_to_atlas = [](int u8, int v8, float& out_u, float& out_v)
    {
        const int tile_col = (u8 >> 5) & 7;
        const int within_x =  u8 & 31;
        const int tile_row =  v8 >> 5;
        const int within_y =  v8 & 31;
        const int tile_id  = tile_row * (int)block_count_per_row + tile_col;
        float u0f, v0f, u1f, v1f;
        TileAtlasPacker::GetTileUV(tile_id, &u0f, &v0f, &u1f, &v1f);
        out_u = u0f + ((float)within_x / 32.0f) * (u1f - u0f);
        out_v = v0f + ((float)within_y / 32.0f) * (v1f - v0f);
    };

    const float z_ndc = 2.0f * (float)m_current_bucket / (float)(BUCKETS_COUNT - 1) - 1.0f;

    WorldVertex* wv = &m_verts[m_vert_count];
    COMPACT_UV_TO_WORLDVERTEX(&wv[0], sx0, sy0, u0, v0, shade0, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&wv[1], sx1, sy1, u1, v1, shade1, m_screen_w, m_screen_h);
    COMPACT_UV_TO_WORLDVERTEX(&wv[2], sx2, sy2, u2, v2, shade2, m_screen_w, m_screen_h);
    compact_to_atlas(u0, v0, wv[0].u, wv[0].v);
    compact_to_atlas(u1, v1, wv[1].u, wv[1].v);
    compact_to_atlas(u2, v2, wv[2].u, wv[2].v);
    wv[0].z = z_ndc; wv[1].z = z_ndc; wv[2].z = z_ndc;
    m_vert_count += 3;
    return true;
}

bool VKWorldViewRenderer::append_frontview_quad(const struct BucketKindTexturedQuad* txquad)
{
    // Mirror GLWorldViewRenderer::append_frontview_quad() exactly.
    // Front view has no source world-space vertex data yet; append_triangle() defaults to 0.
    int orient = txquad->orient & 3;

    PolyPoint a, d, b, c;
    a.X = (txquad->texture_x)                  >> 8;
    a.Y = (txquad->texture_y)                  >> 8;
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

    bool ok = append_triangle(tile_id, &a, &d, &b);
    ok     &= append_triangle(tile_id, &a, &b, &c);
    return ok;
}

void VKWorldViewRenderer::gpu_flush()
{
    if (!m_initialized || m_vert_count <= m_cmd_vert_start) return;
    DrawCmd cmd;
    cmd.type       = DrawCmd::CMD_TILES;
    cmd.vert_start = m_cmd_vert_start;
    cmd.vert_count = m_vert_count - m_cmd_vert_start;
    m_draw_cmds.push_back(cmd);
    m_cmd_vert_start = m_vert_count;
}

void VKWorldViewRenderer::setup_world_sprite_processing(int32_t bucket_num)
{
    if (!m_initialized) return;
    // Half-bucket bias ensures sprites always pass depth test against same-bucket tiles.
    m_current_sprite_z = 2.0f * ((float)bucket_num - 0.5f) / (float)(BUCKETS_COUNT - 1) - 1.0f;
}

/******************************************************************************/
// SubmitKeeperSprite / ClearKeeperSpriteAtlas / PreloadKeeperSpriteAtlas
/******************************************************************************/

int VKWorldViewRenderer::SubmitKeeperSprite(
    int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
    const unsigned char* data, int src_w, int src_h,
    unsigned int draw_flags, const unsigned char* remap)
{
    ASSERT_GAME_THREAD();
    if (src_w <= 0 || src_h <= 0 || src_w > k_kspr_decode_dim || src_h > k_kspr_decode_dim) return 1;
    if (dst_w <= 0 || dst_h <= 0) return 1;

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
    m_kspr_ir.push_back(cmd);
    return 1;
}

int VKWorldViewRenderer::SubmitWorldShadowCmd(const IRWorldShadowCmd& cmd)
{
    ASSERT_GAME_THREAD();
    if (!m_world_write_cmds)
        return 0;
    m_world_write_cmds->shadows.Append(cmd);
    return 1;
}

void VKWorldViewRenderer::ClearKeeperSpriteAtlas()
{
    // Called between levels. Free the keeper-sprite atlas so it will be
    // rebuilt for the new level's sprite set. (Same as GLWorldViewRenderer.)
    // Safe to call from the game thread since no render thread work is in flight
    // at level boundary.
    free_kspr_atlas();
    m_kspr_clut_used = 1;
    m_kspr_clut_map.clear();
    m_clut_dirty = true;
}

void VKWorldViewRenderer::PreloadKeeperSpriteAtlas()
{
    // Preloading is handled lazily on the render thread in DrawKeeperSpriteVK().
    // Nothing to do here for the VK path.
}

/******************************************************************************/
// FlipBuffers
/******************************************************************************/

void VKWorldViewRenderer::FlipBuffers()
{
    ASSERT_GAME_THREAD();

    m_rt_draw_cmds      = std::move(m_draw_cmds);
    m_rt_flatpoly_verts = std::move(m_flatpoly_verts);
    m_rt_kspr_ir        = std::move(m_kspr_ir);
    m_rt_screen_w       = m_screen_w;
    m_rt_screen_h       = m_screen_h;
    m_rt_vp_x           = m_vp_x;
    m_rt_vp_y           = m_vp_y;
    if (m_palette_data)
        memcpy(m_rt_palette, m_palette_data, sizeof(m_rt_palette));
    else
        memset(m_rt_palette, 0, sizeof(m_rt_palette));

    // Swap vertex windows: render thread gets the filled window, game thread
    // gets a clean window in the other half of the CPU_TO_GPU buffer.
    std::swap(m_verts, m_rt_verts);
    m_rt_vert_count  = m_vert_count;
    m_vert_count     = 0;
    m_cmd_vert_start = 0;
}

/******************************************************************************/
// ExecuteFromIR (render thread)
/******************************************************************************/

void VKWorldViewRenderer::ExecuteFromIR(const WorldCommandBuffers& cmds)
{
    ASSERT_RENDER_THREAD();
    if (!m_initialized || (m_rt_draw_cmds.empty() && cmds.shadows.Empty())) return;

    execute_passes(m_cmd, m_rt_vp_x, m_rt_vp_y, m_rt_screen_w, m_rt_screen_h, m_rt_kspr_ir, cmds.shadows);
    m_rt_kspr_ir.clear();
}

/******************************************************************************/
// execute_passes (render thread)
/******************************************************************************/

void VKWorldViewRenderer::execute_passes(VkCommandBuffer cmd,
                                          int vp_x, int vp_y, int screen_w, int screen_h,
                                          const std::vector<IRWorldKeeperSpriteCmd>& kspr_ir,
                                          const IRCommandBuffer<IRWorldShadowCmd>& shadows)
{
    if (!cmd) return;
    ensure_clut_valid(cmd);

    // Reset transient vertex cursor for this frame.
    // The render-thread window is the second half (frame_index=1) or first half (0).
    const int transient_vtx_stride = 5 * sizeof(float);
    const VkDeviceSize transient_window = (VkDeviceSize)m_rt_frame_index
                                        * k_max_transient_verts * transient_vtx_stride;
    m_transient_cursor = 0;

    // ── Bind the world vertex buffer ─────────────────────────────────────────
    // The render-thread window (m_rt_verts) is already written by the game
    // thread; we just need to tell VK where in the VMA buffer it lives.
    const VkDeviceSize vtx_window_offset = (m_rt_verts - m_vtx_mapped) * sizeof(WorldVertex);

    VkViewport vp = {};
    vp.x        = (float)vp_x;
    vp.y        = (float)vp_y;
    vp.width    = (float)screen_w;
    vp.height   = (float)screen_h;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor = {};
    scissor.offset = { vp_x, vp_y };
    scissor.extent = { (uint32_t)screen_w, (uint32_t)screen_h };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Tile atlas: VKTileAtlas provides a 2D array view.
    VkImageView  tile_atlas_view    = m_tile_atlas ? m_tile_atlas->GetView()    : VK_NULL_HANDLE;
    VkSampler    tile_atlas_sampler = m_tile_atlas ? m_tile_atlas->GetSampler() : VK_NULL_HANDLE;
    if (!tile_atlas_view) {
        // No atlas yet — skip tile draws this frame.
        return;
    }

    // Build world tile push constants from renderer settings.
    // Uses the shared push-constant block layout defined in VKShaders.h.
    // We write the full 128-byte struct so all fields are defined.
    struct {
        float screen_size[2];  // [0..7]
        float z_ndc;           // [8..11]
        float alpha;           // [12..15]
        float clip_rect[4];    // [16..31]
        float clip_radius;     // [32..35]
        float clip_screen_h;   // [36..39]
        float remap_row;       // [40..43]
        float tint_factor;     // [44..47]
        float center_map[2];   // [48..55]
        float screen_center[2];// [56..63]
        float zoom_scale;      // [64..67]
        float inv_map_size[2]; // [68..75]
        float map_step;        // [76..79]
        float fullbright;      // [80..83]
        float ambient;         // [84..87]
        float shade_scale;     // [88..91]
        float shade_gamma;     // [92..95]
        int   lighting_mode;   // [96..99]
        int   darkness_mode;   // [100..103]
        int   tile_filter;     // [104..107]
        float missing_tile;    // [108..111]
        float time;            // [112..115]
        float fog_speed;       // [116..119]
        float fog_density;     // [120..123]
        float ndc_z_shadow;    // [124..127]
    } pc = {};

    pc.screen_size[0] = (float)screen_w;
    pc.screen_size[1] = (float)screen_h;
    pc.z_ndc          = 0.0f;
    pc.alpha          = 1.0f;
    pc.clip_radius    = -1.0f;  // no clip
    pc.fullbright     = g_renderer_settings.shade_fullbright;
    pc.ambient        = g_renderer_settings.shade_ambient;
    pc.shade_scale    = g_renderer_settings.shade_scale;
    pc.shade_gamma    = std::max(0.0f, g_renderer_settings.shade_gamma);
    pc.lighting_mode  = g_renderer_settings.lighting_mode;
    pc.darkness_mode  = g_renderer_settings.darkness_mode;
    pc.tile_filter    = g_renderer_settings.tile_filter;
    pc.missing_tile   = (tile_atlas_view == VK_NULL_HANDLE) ? 1.0f : 0.0f;
    pc.fog_speed      = g_renderer_settings.fog_speed;
    pc.fog_density    = g_renderer_settings.fog_density;
    {
        static auto t0 = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        pc.time = std::chrono::duration<float>(now - t0).count();
    }

    // ── Pass 1: Opaque geometry (tiles + flat polys) ──────────────────────────
    VkPipeline tile_pipeline = m_pipelines->GetPipeline(VKPassType::WorldTile);
    VkPipeline fp_pipeline   = m_pipelines->GetPipeline(VKPassType::WorldFlatPoly);
    if (!tile_pipeline) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tile_pipeline);
    vkCmdPushConstants(cmd, m_pipelines->GetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), &pc);

    // Bind tile atlas descriptor set: set=0 binding=0=atlas_array, 1=palette, 3=fade_table
    VkDescriptorSet tile_ds = m_desc_layout->Alloc();
    if (tile_ds != VK_NULL_HANDLE) {
        VkImageView  views[4]    = { tile_atlas_view,  m_palette_view, VK_NULL_HANDLE, m_fade_view };
        VkSampler    samplers[4] = { tile_atlas_sampler, m_palette_sampler, VK_NULL_HANDLE, m_fade_sampler };
        // Binding 0: tile atlas (sampler2DArray), 1: palette, 3: fade_table
        // Binding 2 (lightmap) is left unbound in the initial pass.
        m_desc_layout->UpdateCombinedImageSampler(tile_ds, 0, tile_atlas_view,  tile_atlas_sampler);
        m_desc_layout->UpdateCombinedImageSampler(tile_ds, 1, m_palette_view,   m_palette_sampler);
        m_desc_layout->UpdateCombinedImageSampler(tile_ds, 3, m_fade_view,      m_fade_sampler);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelines->GetLayout(), 0, 1, &tile_ds, 0, nullptr);
    }

    VkDeviceSize vtx_off = vtx_window_offset;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vtx_buf, &vtx_off);

    bool fp_uploaded = false;
    for (const auto& dc : m_rt_draw_cmds) {
        if (dc.type == DrawCmd::CMD_TILES) {
            vkCmdDraw(cmd, (uint32_t)dc.vert_count, 1, (uint32_t)dc.vert_start, 0);
        } else if (dc.type == DrawCmd::CMD_FLAT_POLYS && fp_pipeline) {
            if (!m_rt_flatpoly_verts.empty()) {
                if (!fp_uploaded) {
                    // Copy flat-poly vertices into the second window of m_fp_buf.
                    const VkDeviceSize fp_off = (m_rt_frame_index == 0) ? 0
                                              : (VkDeviceSize)k_max_flatpoly_verts * sizeof(FlatPolyVertex);
                    const size_t fp_bytes = m_rt_flatpoly_verts.size() * sizeof(FlatPolyVertex);
                    memcpy((uint8_t*)m_fp_mapped + fp_off, m_rt_flatpoly_verts.data(), fp_bytes);
                    fp_uploaded = true;
                }
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fp_pipeline);
                VkDeviceSize fp_bind_off = (m_rt_frame_index == 0) ? 0
                                         : (VkDeviceSize)k_max_flatpoly_verts * sizeof(FlatPolyVertex);
                vkCmdBindVertexBuffers(cmd, 0, 1, &m_fp_buf, &fp_bind_off);
                vkCmdDraw(cmd, (uint32_t)dc.vert_count, 1, (uint32_t)dc.vert_start, 0);
                // Restore tile pipeline and bindings.
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tile_pipeline);
                vkCmdBindVertexBuffers(cmd, 0, 1, &m_vtx_buf, &vtx_off);
            }
        }
    }

    // ── Pass 2: Shadows ───────────────────────────────────────────────────────
    VkPipeline shadow_pipeline = m_pipelines->GetPipeline(VKPassType::WorldShadow);
    if (shadow_pipeline) {
        int shadow_count = 0;
        const int shadow_limit = g_renderer_settings.shadow_max_count;
        for (const auto& sc : shadows) {
            if (shadow_limit > 0 && shadow_count >= shadow_limit)
                break;
            if (sc.tex_w <= 0 || sc.tex_h <= 0 || sc.tex_w > 256 || sc.tex_h > 256)
                continue;

            memset(s_vk_kspr_decode_buf, 0, (size_t)sc.tex_h * k_kspr_decode_dim);
            draw_keepsprite_unscaled_in_buffer(sc.anim_sprite, sc.angle,
                                               sc.current_frame, s_vk_kspr_decode_buf);

            const size_t shadow_bytes = (size_t)sc.tex_w * sc.tex_h;
            VKStagingAlloc sa;
            if (!m_staging->Alloc(nullptr, shadow_bytes, 1, sa)) continue;
            uint8_t* dst = static_cast<uint8_t*>(sa.cpu_ptr);
            for (int y = 0; y < sc.tex_h; ++y)
                memcpy(dst + y * sc.tex_w, s_vk_kspr_decode_buf + y * k_kspr_decode_dim, sc.tex_w);

            VkImageMemoryBarrier b = {};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_shadow_img;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);

            VkBufferImageCopy region = {};
            region.bufferOffset                    = sa.offset;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount     = 1;
            region.imageExtent                     = { (uint32_t)sc.tex_w, (uint32_t)sc.tex_h, 1 };
            vkCmdCopyBufferToImage(cmd, sa.buffer, m_shadow_img,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);

            static const int k_shadow_verts = 6;
            struct ShadVtx { float x, y, u, v; };
            ShadVtx sv[k_shadow_verts];
            const EnginePolyVertex* vp = sc.verts;
            const int idx[k_shadow_verts] = { 0, 1, 2, 0, 2, 3 };
            for (int t = 0; t < k_shadow_verts; t++) {
                sv[t].x = (float)vp[idx[t]].x;
                sv[t].y = (float)vp[idx[t]].y;
                sv[t].u = (float)(vp[idx[t]].u >> 16) / 256.0f;
                sv[t].v = (float)(vp[idx[t]].v >> 16) / 256.0f;
            }
            const VkDeviceSize sv_byte_off = transient_window + m_transient_cursor;
            if (m_transient_cursor + (int)sizeof(sv) > k_max_transient_verts * (int)transient_vtx_stride)
                continue;
            memcpy(m_transient_mapped + sv_byte_off, sv, sizeof(sv));
            m_transient_cursor += (int)sizeof(sv);

            uint8_t full_pc[128] = {};
            memcpy(full_pc, &pc, sizeof(pc));
            float* darkness_field = reinterpret_cast<float*>(full_pc + 12);
            *darkness_field = sc.darkness * g_renderer_settings.shadow_darkness_scale;
            float* ndc_z_shadow   = reinterpret_cast<float*>(full_pc + 124);
            *ndc_z_shadow = sc.ndc_z;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_pipeline);
            vkCmdPushConstants(cmd, m_pipelines->GetLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, 128, full_pc);

            VkDescriptorSet shadow_ds = m_desc_layout->Alloc();
            if (shadow_ds != VK_NULL_HANDLE) {
                m_desc_layout->UpdateCombinedImageSampler(shadow_ds, 0, m_shadow_view, m_shadow_sampler);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_pipelines->GetLayout(), 0, 1, &shadow_ds, 0, nullptr);
            }

            VkDeviceSize sv_off = sv_byte_off;
            vkCmdBindVertexBuffers(cmd, 0, 1, &m_transient_buf, &sv_off);
            vkCmdDraw(cmd, k_shadow_verts, 1, 0, 0);
            shadow_count++;
        }
    }

    // ── Pass 3: Keeper sprites ────────────────────────────────────────────────
    for (const auto& dc : m_rt_draw_cmds) {
        if (dc.type != DrawCmd::CMD_IR_KEEPER_SPRITES) continue;
        for (int i = dc.sprite_ir_start; i < dc.sprite_ir_start + dc.sprite_ir_count; ++i) {
            if (i >= (int)kspr_ir.size()) break;
            DrawKeeperSpriteVK(cmd, kspr_ir[i]);
        }
    }
}

/******************************************************************************/
// DrawKeeperSpriteVK (render thread)
/******************************************************************************/

void VKWorldViewRenderer::DrawKeeperSpriteVK(VkCommandBuffer cmd,
                                              const IRWorldKeeperSpriteCmd& kspr)
{
    if (!kspr.data || kspr.src_w <= 0 || kspr.src_h <= 0) return;
    if (!m_kspr_image) return;

    const bool additive = (kspr.draw_flags & Lb_SPRITE_ALPHA_ADDITIVE) != 0;

    // ── Atlas cache lookup ────────────────────────────────────────────────────
    int atlas_layer = -1;
    auto it = m_kspr_atlas_map.find(kspr.data);
    if (it != m_kspr_atlas_map.end()) {
        atlas_layer = it->second.layer;
        ++m_kspr_atlas_hits;
    } else if (m_kspr_atlas_used < k_kspr_atlas_layers) {
        atlas_layer = m_kspr_atlas_used++;
        AtlasEntry entry; entry.layer = atlas_layer; entry.src_w = kspr.src_w;
        m_kspr_atlas_map[kspr.data] = entry;

        // Decode RLE into scratch buffer.
        decode_keeper_rle_vk(s_vk_kspr_decode_buf, kspr.data, kspr.src_w, kspr.src_h);

        // Upload decoded sprite to atlas layer via staging ring.
        const size_t decoded_bytes = (size_t)kspr.src_w * kspr.src_h;
        VKStagingAlloc sa;
        if (m_staging->Alloc(nullptr, decoded_bytes, 1, sa)) {
            uint8_t* dst_ptr = static_cast<uint8_t*>(sa.cpu_ptr);
            for (int y = 0; y < kspr.src_h; ++y)
                memcpy(dst_ptr + y * kspr.src_w, s_vk_kspr_decode_buf + y * k_kspr_decode_dim, kspr.src_w);

            // Transition atlas layer to TRANSFER_DST.
            VkImageMemoryBarrier b = {};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_kspr_image;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, (uint32_t)atlas_layer, 1 };
            b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);

            VkBufferImageCopy region = {};
            region.bufferOffset                    = sa.offset;
            region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel       = 0;
            region.imageSubresource.baseArrayLayer = (uint32_t)atlas_layer;
            region.imageSubresource.layerCount     = 1;
            region.imageExtent                     = { (uint32_t)kspr.src_w, (uint32_t)kspr.src_h, 1 };
            vkCmdCopyBufferToImage(cmd, sa.buffer, m_kspr_image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            b.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }
    } else {
        // Atlas full — skip this sprite rather than rendering garbage.
        return;
    }

    // ── CLUT lookup ───────────────────────────────────────────────────────────
    int clut_row = 0;
    if (kspr.remap && (kspr.draw_flags & Lb_TEXT_UNDERLNSHADOW) && !additive)
        clut_row = alloc_clut_row(kspr.remap);

    // ── Build sprite quad (inline via staging) ────────────────────────────────
    const float u1 = (float)kspr.src_w / (float)k_kspr_decode_dim;
    const float v1 = (float)kspr.src_h / (float)k_kspr_decode_dim;

    const float screen_w = (float)(m_rt_screen_w > 0 ? m_rt_screen_w : 1);
    const float screen_h = (float)(m_rt_screen_h > 0 ? m_rt_screen_h : 1);

    const float nx0 = (float)kspr.dst_x / screen_w * 2.0f - 1.0f;
    const float ny0 = 1.0f - (float)kspr.dst_y / screen_h * 2.0f;
    const float nx1 = (float)(kspr.dst_x + kspr.dst_w) / screen_w * 2.0f - 1.0f;
    const float ny1 = 1.0f - (float)(kspr.dst_y + kspr.dst_h) / screen_h * 2.0f;

    // Sprite vertex: x, y, u, v, layer (5 floats; matches WorldSpriteArray shader)
    struct KSprVtx { float x, y, u, v, layer; };
    KSprVtx sv[6] = {
        { nx0, ny0, 0.0f, 0.0f,  (float)atlas_layer },
        { nx1, ny0, u1,   0.0f,  (float)atlas_layer },
        { nx1, ny1, u1,   v1,    (float)atlas_layer },
        { nx0, ny0, 0.0f, 0.0f,  (float)atlas_layer },
        { nx1, ny1, u1,   v1,    (float)atlas_layer },
        { nx0, ny1, 0.0f, v1,    (float)atlas_layer },
    };

    // Write sprite quad to the transient vertex buffer (VERTEX_BUFFER_BIT).
    const int transient_vtx_stride = 5 * (int)sizeof(float);
    const VkDeviceSize transient_window = (VkDeviceSize)m_rt_frame_index
                                        * k_max_transient_verts * transient_vtx_stride;
    if (m_transient_cursor + (int)sizeof(sv) > k_max_transient_verts * transient_vtx_stride)
        return;  // transient buffer full this frame
    const VkDeviceSize sv_byte_off = transient_window + m_transient_cursor;
    memcpy(m_transient_mapped + sv_byte_off, sv, sizeof(sv));
    m_transient_cursor += (int)sizeof(sv);

    // Push constants: use screen_size, z_ndc, alpha; remap_row = clut_row.
    // We reuse the shared 128-byte PC struct — write a partial version.
    float pc_sprite[32] = {};
    pc_sprite[0]  = screen_w;
    pc_sprite[1]  = screen_h;
    pc_sprite[2]  = kspr.z_ndc;  // z_ndc
    pc_sprite[3]  = 1.0f;        // alpha
    pc_sprite[10] = (float)clut_row; // remap_row at offset 40

    VKPassType pass = additive ? VKPassType::WorldGlow : VKPassType::WorldSpriteArray;
    VkPipeline pipeline = m_pipelines->GetPipeline(pass);
    if (!pipeline) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(cmd, m_pipelines->GetLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc_sprite), pc_sprite);

    VkDescriptorSet sprite_ds = m_desc_layout->Alloc();
    if (sprite_ds != VK_NULL_HANDLE) {
        m_desc_layout->UpdateCombinedImageSampler(sprite_ds, 0, m_kspr_view,    m_kspr_sampler);
        m_desc_layout->UpdateCombinedImageSampler(sprite_ds, 1, m_clut_view,    m_clut_sampler);
        m_desc_layout->UpdateCombinedImageSampler(sprite_ds, 2, m_palette_view, m_palette_sampler);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipelines->GetLayout(), 0, 1, &sprite_ds, 0, nullptr);
    }

    VkDeviceSize sv_off = sv_byte_off;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_transient_buf, &sv_off);
    vkCmdDraw(cmd, 6, 1, 0, 0);
}

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
