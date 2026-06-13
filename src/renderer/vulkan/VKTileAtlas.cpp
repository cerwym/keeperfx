/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKTileAtlas.cpp
 *     Vulkan tile texture atlas implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKTileAtlas.h"

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include "bflib_basics.h"
#include "engine_textures.h" // TEXTURE_BLOCKS_COUNT, block_ptrs[]
#include "bflib_video.h"     // lbPalette (for R8 decode)
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

void VKTileAtlas::SetDevice(VkDevice device, VmaAllocator allocator)
{
    m_device    = device;
    m_allocator = allocator;
}

bool VKTileAtlas::Init()
{
    if (!InitPacker()) {
        ERRORLOG("VKTileAtlas: InitPacker failed");
        return false;
    }

    const size_t layer_bytes = (size_t)k_atlas_w * k_atlas_h;
    m_r8_scratch = new uint8_t[layer_bytes];

    for (int v = 0; v < k_max_variations; ++v) {
        m_cpu_pixels[v]  = new uint8_t[layer_bytes];
        m_dirty[v]       = false;
        m_dirty_y_min[v] = k_atlas_h;
        m_dirty_y_max[v] = -1;
    }
    m_image_needs_init = true;

    // Decode all variations into CPU buffers (GPU upload deferred to FlushPendingVK).
    for (int v = 0; v < k_max_variations; ++v) {
        BuildVariation(v);  // calls UploadFull(v) → marks m_dirty[v] = true
    }

    m_initialized = true;
    return true;
}

void VKTileAtlas::Free()
{
    if (m_sampler != VK_NULL_HANDLE) { vkDestroySampler(m_device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
    if (m_view    != VK_NULL_HANDLE) { vkDestroyImageView(m_device, m_view, nullptr);  m_view    = VK_NULL_HANDLE; }
    if (m_image   != VK_NULL_HANDLE) { vmaDestroyImage(m_allocator, m_image, m_image_alloc); m_image = VK_NULL_HANDLE; }
    for (int v = 0; v < k_max_variations; ++v) {
        delete[] m_cpu_pixels[v];
        m_cpu_pixels[v] = nullptr;
    }
    delete[] m_r8_scratch; m_r8_scratch = nullptr;
    FreePacker();
    m_initialized      = false;
    m_image_needs_init = true;
}

void VKTileAtlas::UpdateAnimatedTiles()
{
    for (int v = 0; v < k_max_variations; ++v) {
        BuildAnimatedStrip(v);
    }
}

unsigned int VKTileAtlas::GetAtlasTexture(int /*variation*/) const { return 0; }

/******************************************************************************/
// TileAtlasPacker overrides
/******************************************************************************/

void VKTileAtlas::BuildVariation(int variation)
{
    // Use the base TileAtlasPacker R8 override to fill m_r8_scratch.
    // Base class BuildVariation fills m_rgba_scratch with RGBA8 data.
    // We override to fill m_r8_scratch with palette indices directly
    // (same as GLTileAtlas which also uses R8 format).
    TileAtlasPacker::BuildVariation(variation);
    // After base class builds m_rgba_scratch (RGBA8), UploadFull is called.
}

void VKTileAtlas::BuildAnimatedStrip(int variation)
{
    TileAtlasPacker::BuildAnimatedStrip(variation);
}

void VKTileAtlas::UploadFull(int variation)
{
    if (variation < 0 || variation >= k_max_variations) return;
    if (!m_cpu_pixels[variation]) return;

    // The base class has filled m_rgba_scratch with RGBA8 data.
    // For R8 we need just the red channel (palette index). Copy it.
    const size_t total = (size_t)k_atlas_w * k_atlas_h;
    const uint8_t* src = m_rgba_scratch;
    uint8_t* dst       = m_cpu_pixels[variation];
    for (size_t i = 0; i < total; ++i)
        dst[i] = src[i * 4 + 0];   // R = palette index (base packs R = index, G/B/A = 0)

    m_dirty[variation]       = true;
    m_dirty_y_min[variation] = 0;
    m_dirty_y_max[variation] = k_atlas_h;
}

void VKTileAtlas::UploadAnimatedStrip(int variation, int y_offset, int h_pixels)
{
    if (variation < 0 || variation >= k_max_variations) return;
    if (!m_cpu_pixels[variation]) return;

    const uint8_t* src = m_rgba_scratch + (size_t)y_offset * k_atlas_w * 4;
    uint8_t* dst       = m_cpu_pixels[variation] + (size_t)y_offset * k_atlas_w;
    const size_t total = (size_t)k_atlas_w * h_pixels;
    for (size_t i = 0; i < total; ++i)
        dst[i] = src[i * 4 + 0];

    m_dirty[variation]       = true;
    m_dirty_y_min[variation] = std::min(m_dirty_y_min[variation], y_offset);
    m_dirty_y_max[variation] = std::max(m_dirty_y_max[variation], y_offset + h_pixels);
}

/******************************************************************************/
// FlushPendingVK — GPU upload
/******************************************************************************/

void VKTileAtlas::FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging)
{
    // Check if any layer is dirty.
    bool any_dirty = m_image_needs_init;
    for (int v = 0; v < k_max_variations && !any_dirty; ++v)
        if (m_dirty[v]) any_dirty = true;
    if (!any_dirty) return;

    // Create the VkImage on first use.
    if (m_image == VK_NULL_HANDLE) {
        VkImageCreateInfo img = {};
        img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img.imageType     = VK_IMAGE_TYPE_2D;
        img.format        = VK_FORMAT_R8_UNORM;
        img.extent        = { (uint32_t)k_atlas_w, (uint32_t)k_atlas_h, 1 };
        img.mipLevels     = 1;
        img.arrayLayers   = (uint32_t)k_max_variations;
        img.samples       = VK_SAMPLE_COUNT_1_BIT;
        img.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo ai = {}; ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(m_allocator, &img, &ai, &m_image, &m_image_alloc, nullptr) != VK_SUCCESS) {
            ERRORLOG("VKTileAtlas: vmaCreateImage failed"); return;
        }

        VkImageViewCreateInfo vi = {};
        vi.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image    = m_image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        vi.format   = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)k_max_variations };
        if (vkCreateImageView(m_device, &vi, nullptr, &m_view) != VK_SUCCESS) {
            ERRORLOG("VKTileAtlas: vkCreateImageView failed"); return;
        }

        VkSamplerCreateInfo si = {};
        si.sType      = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter  = VK_FILTER_NEAREST;
        si.minFilter  = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        if (vkCreateSampler(m_device, &si, nullptr, &m_sampler) != VK_SUCCESS) {
            ERRORLOG("VKTileAtlas: vkCreateSampler failed"); return;
        }

        // Transition all layers to TRANSFER_DST_OPTIMAL.
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)k_max_variations };
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);

        // Mark all variations dirty for initial upload.
        for (int v = 0; v < k_max_variations; ++v) {
            m_dirty[v]       = true;
            m_dirty_y_min[v] = 0;
            m_dirty_y_max[v] = k_atlas_h;
        }
        m_image_needs_init = false;
    } else {
        // Transition dirty layers from SHADER_READ to TRANSFER_DST.
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)k_max_variations };
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // Upload dirty layers.
    const size_t layer_bytes = (size_t)k_atlas_w * k_atlas_h;
    for (int v = 0; v < k_max_variations; ++v) {
        if (!m_dirty[v] || !m_cpu_pixels[v]) continue;
        int y_min = m_dirty_y_min[v];
        int y_max = m_dirty_y_max[v];
        if (y_min >= y_max) continue;

        const int    strip_h     = y_max - y_min;
        const size_t strip_bytes = (size_t)k_atlas_w * strip_h;
        VKStagingAlloc sa;
        if (!staging.Alloc(m_cpu_pixels[v] + (size_t)y_min * k_atlas_w, strip_bytes, 1, sa)) {
            WARNLOG("VKTileAtlas: staging ring full at variation %d", v);
            continue;
        }

        VkBufferImageCopy region = {};
        region.bufferOffset                    = sa.offset;
        region.bufferRowLength                 = 0;   // tightly packed
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = (uint32_t)v;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { 0, y_min, 0 };
        region.imageExtent                     = { (uint32_t)k_atlas_w, (uint32_t)strip_h, 1 };
        vkCmdCopyBufferToImage(cmd, sa.buffer, m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        m_dirty[v]       = false;
        m_dirty_y_min[v] = k_atlas_h;
        m_dirty_y_max[v] = -1;
    }

    // Transition all layers to SHADER_READ_ONLY_OPTIMAL.
    VkImageMemoryBarrier b2 = {};
    b2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b2.image = m_image;
    b2.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, (uint32_t)k_max_variations };
    b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &b2);
}

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
