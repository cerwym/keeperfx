/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKFontAtlas.cpp
 *     Vulkan font texture atlas — implementation.
 *     Glyph packing logic mirrors GLFontAtlas.cpp.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKFontAtlas.h"

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/vulkan/VKStagingRing.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_basics.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include "post_inc.h"

/******************************************************************************/

namespace {
constexpr long kMaxFontSpriteCountSanity = 4096;
constexpr int  kMinAtlasSide = 256;
constexpr int  kMaxAtlasSide = 2048;
}

/******************************************************************************/

void VKFontAtlas::SetDevice(VkDevice device, VmaAllocator allocator)
{
    m_device    = device;
    m_allocator = allocator;
}

void VKFontAtlas::Shutdown()
{
    if (m_device != VK_NULL_HANDLE) {
        if (m_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(m_device, m_sampler, nullptr);
            m_sampler = VK_NULL_HANDLE;
        }
        if (m_view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_device, m_view, nullptr);
            m_view = VK_NULL_HANDLE;
        }
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_allocator, m_image, m_image_alloc);
            m_image       = VK_NULL_HANDLE;
            m_image_alloc = VK_NULL_HANDLE;
        }
    }
    m_pixels.clear();
    m_glyphs.clear();
    m_atlas_width    = 0;
    m_atlas_height   = 0;
    m_line_height    = 0;
    m_max_char_width = 0;
    m_sprite_count   = 0;
    m_vk_init_needed = false;
}

bool VKFontAtlas::NeedsRebuild(const struct TbSpriteSheet* font_sheet) const
{
    if (!font_sheet) return false;
    const long n = num_sprites(font_sheet);
    return (n > 0 && n <= kMaxFontSpriteCountSanity) && (n != m_sprite_count);
}

bool VKFontAtlas::Init(const struct TbSpriteSheet* font_sheet)
{
    if (!font_sheet) { ERRORLOG("VKFontAtlas: null font sheet"); return false; }

    const long sprite_count = num_sprites(font_sheet);
    if (sprite_count <= 0 || sprite_count > kMaxFontSpriteCountSanity) {
        ERRORLOG("VKFontAtlas: suspicious font sheet (sprite_count=%ld)", sprite_count);
        return false;
    }

    // Calculate atlas dimensions (same algorithm as GL)
    int total_area = 0, max_w = 0, max_h = 0;
    for (long i = 0; i < sprite_count && i < 256; ++i) {
        const struct TbSprite* spr = get_sprite(font_sheet, i);
        if (spr && spr->SWidth > 0 && spr->SHeight > 0) {
            total_area += spr->SWidth * spr->SHeight;
            max_w = std::max(max_w, (int)spr->SWidth);
            max_h = std::max(max_h, (int)spr->SHeight);
        }
    }
    m_line_height    = max_h;
    m_max_char_width = max_w;

    int side = (int)sqrt(total_area * 1.5f);
    int atlas_w = kMinAtlasSide;
    while (atlas_w < side) atlas_w <<= 1;
    atlas_w = std::min(atlas_w, kMaxAtlasSide);
    int atlas_h = std::max(atlas_w, kMinAtlasSide);
    m_atlas_width  = atlas_w;
    m_atlas_height = atlas_h;

    // Allocate RGBA8 CPU atlas buffer
    m_pixels.assign((size_t)atlas_w * atlas_h * 4, 0);
    m_glyphs.assign(256, VKFontGlyph{});

    // Pack glyphs into atlas
    int cur_x = 0, cur_y = 0, row_h = 0;
    for (long i = 0; i < sprite_count && i < 256; ++i) {
        const struct TbSprite* spr = get_sprite(font_sheet, i);
        if (!spr || spr->SWidth == 0 || spr->SHeight == 0) continue;

        const int cw = spr->SWidth, ch = spr->SHeight;
        if (cur_x + cw > atlas_w) { cur_x = 0; cur_y += row_h; row_h = 0; }
        if (cur_y + ch > atlas_h) {
            ERRORLOG("VKFontAtlas: atlas full at glyph %ld", i);
            break;
        }

        m_glyphs[i].u0 = (float) cur_x       / (float)atlas_w;
        m_glyphs[i].v0 = (float) cur_y        / (float)atlas_h;
        m_glyphs[i].u1 = (float)(cur_x + cw)  / (float)atlas_w;
        m_glyphs[i].v1 = (float)(cur_y + ch)  / (float)atlas_h;
        m_glyphs[i].width  = cw;
        m_glyphs[i].height = ch;

        // Decode RLE into CPU buffer
        std::vector<uint8_t> tmp((size_t)cw * ch, 0);
        if (DecodeRLESprite(tmp.data(), cw, spr->Data, cw, ch)) {
            for (int y = 0; y < ch; ++y) {
                for (int x = 0; x < cw; ++x) {
                    const size_t atlas_off = ((size_t)(cur_y + y) * atlas_w + (cur_x + x)) * 4;
                    const uint8_t idx = tmp[(size_t)y * cw + x];
                    m_pixels[atlas_off + 0] = idx;
                    m_pixels[atlas_off + 1] = 0;
                    m_pixels[atlas_off + 2] = 0;
                    m_pixels[atlas_off + 3] = (idx == 0) ? 0 : 255;
                }
            }
        }
        cur_x += cw;
        row_h  = std::max(row_h, ch);
    }

    m_sprite_count   = sprite_count;
    m_vk_init_needed = true;
    SYNCLOG("VKFontAtlas: CPU init done (%dx%d RGBA8, %ld glyphs)", atlas_w, atlas_h, sprite_count);
    return true;
}

const VKFontGlyph* VKFontAtlas::GetGlyph(unsigned long chr) const
{
    if (chr >= 256 || m_glyphs.empty()) return nullptr;
    const VKFontGlyph* g = &m_glyphs[chr];
    return (g->width > 0) ? g : nullptr;
}

/******************************************************************************/

static void vkfa_transition(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout old_l, VkImageLayout new_l,
                             VkAccessFlags sa, VkPipelineStageFlags ss,
                             VkAccessFlags da, VkPipelineStageFlags ds)
{
    VkImageMemoryBarrier b = {};
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = old_l; b.newLayout = new_l;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask       = sa; b.dstAccessMask = da;
    vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void VKFontAtlas::FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging)
{
    if (!m_vk_init_needed || m_pixels.empty()) return;

    // Create image
    VkImageCreateInfo img_info = {};
    img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType     = VK_IMAGE_TYPE_2D;
    img_info.format        = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent        = { (uint32_t)m_atlas_width, (uint32_t)m_atlas_height, 1 };
    img_info.mipLevels     = 1;
    img_info.arrayLayers   = 1;
    img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_allocator, &img_info, &alloc_info,
                       &m_image, &m_image_alloc, nullptr) != VK_SUCCESS) {
        ERRORLOG("VKFontAtlas: vmaCreateImage failed"); return;
    }

    VkImageViewCreateInfo view_info = {};
    view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image            = m_image;
    view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format           = VK_FORMAT_R8G8B8A8_UNORM;
    view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_device, &view_info, nullptr, &m_view) != VK_SUCCESS) {
        ERRORLOG("VKFontAtlas: vkCreateImageView failed"); return;
    }

    VkSamplerCreateInfo samp_info = {};
    samp_info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_info.magFilter    = VK_FILTER_NEAREST;
    samp_info.minFilter    = VK_FILTER_NEAREST;
    samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (vkCreateSampler(m_device, &samp_info, nullptr, &m_sampler) != VK_SUCCESS) {
        ERRORLOG("VKFontAtlas: vkCreateSampler failed"); return;
    }

    // Upload
    const VkDeviceSize upload_bytes = (VkDeviceSize)m_atlas_width * m_atlas_height * 4;
    VKStagingAlloc sa;
    if (!staging.Alloc(m_pixels.data(), upload_bytes, 1, sa)) {
        ERRORLOG("VKFontAtlas: staging ring exhausted"); return;
    }

    vkfa_transition(cmd, m_image,
        VK_IMAGE_LAYOUT_UNDEFINED,              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,                                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_ACCESS_TRANSFER_WRITE_BIT,           VK_PIPELINE_STAGE_TRANSFER_BIT);

    staging.CmdCopyToImage(cmd, sa.offset, sa.buffer, m_image,
                           (uint32_t)m_atlas_width, (uint32_t)m_atlas_height);

    vkfa_transition(cmd, m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,           VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_ACCESS_SHADER_READ_BIT,              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    m_vk_init_needed = false;
    SYNCLOG("VKFontAtlas: %dx%d RGBA8 font atlas uploaded", m_atlas_width, m_atlas_height);
}

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
