/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKSpriteAtlas.cpp
 *     Vulkan sprite texture atlas — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKSpriteAtlas.h"

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/vulkan/VKStagingRing.h"
#include "bflib_basics.h"
#include "bflib_sprite.h"
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

void VKSpriteAtlas::SetDevice(VkDevice device, VmaAllocator allocator,
                               VkCommandPool cmd_pool, VkQueue transfer_queue)
{
    m_device    = device;
    m_allocator = allocator;
    m_cmd_pool  = cmd_pool;
    m_queue     = transfer_queue;
}

bool VKSpriteAtlas::Init()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return Init_Internal();
}

bool VKSpriteAtlas::Init_Internal()
{
    m_pixels.assign((size_t)k_atlas_w * k_atlas_h, 0u);
    m_cursor_x       = 1;
    m_shelf_y        = 0;
    m_shelf_h        = 0;
    m_dirty_y_min    = k_atlas_h;
    m_dirty_y_max    = -1;
    m_vk_init_needed = true;
    return true;
}

void VKSpriteAtlas::Free()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    Free_Internal();
}

void VKSpriteAtlas::Free_Internal()
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
    m_vk_init_needed = false;
    m_pixels.clear();
    m_sprite_to_handle.clear();
    m_handle_uvs.clear();
    m_next_handle = 0;
}

/******************************************************************************/

bool VKSpriteAtlas::pack_sprite(const struct TbSprite* spr, SpriteUV& out)
{
    const int w = spr->SWidth;
    const int h = spr->SHeight;
    if (w <= 0 || h <= 0) return false;

    const int alloc_w = w + 1;
    const int alloc_h = h + 1;

    if (m_cursor_x + alloc_w > k_atlas_w) {
        m_shelf_y += m_shelf_h;
        m_cursor_x = 0;
        m_shelf_h  = 0;
    }
    if (m_shelf_y + alloc_h > k_atlas_h) {
        ERRORLOG("VKSpriteAtlas: atlas full — cannot pack %dx%d sprite", w, h);
        return false;
    }
    if (alloc_h > m_shelf_h) m_shelf_h = alloc_h;

    uint8_t* dst = m_pixels.data() + m_shelf_y * k_atlas_w + m_cursor_x;
    LbSpriteDecode(dst, k_atlas_w, spr);

    out.u0      = (float) m_cursor_x       / (float)k_atlas_w;
    out.v0      = (float) m_shelf_y        / (float)k_atlas_h;
    out.u1      = (float)(m_cursor_x + w)  / (float)k_atlas_w;
    out.v1      = (float)(m_shelf_y  + h)  / (float)k_atlas_h;
    out.pixel_w = (uint16_t)w;
    out.pixel_h = (uint16_t)h;

    if (m_shelf_y     < m_dirty_y_min) m_dirty_y_min = m_shelf_y;
    if (m_shelf_y + h > m_dirty_y_max) m_dirty_y_max = m_shelf_y + h;

    m_cursor_x += alloc_w;
    return true;
}

/******************************************************************************/

void VKSpriteAtlas::AddSheet(const struct TbSpriteSheet* sheet, const char* name)
{
    if (!sheet) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    AddSheet_Internal(sheet, name);
}

void VKSpriteAtlas::AddSheet_Internal(const struct TbSpriteSheet* sheet, const char* name)
{
    long n = num_sprites(sheet);
    const char* label = name ? name : "(unknown)";
    int packed = 0;

    for (long i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0) continue;
        if (m_sprite_to_handle.count(spr)) continue;
        if (m_next_handle >= kSpriteHandleInvalidIndex) {
            ERRORLOG("VKSpriteAtlas::AddSheet: handle table exhausted");
            break;
        }
        SpriteUV uv;
        if (pack_sprite(spr, uv)) {
            SpriteHandle h = MakeSpriteHandle(m_generation, (uint16_t)m_next_handle++);
            m_sprite_to_handle[spr] = h;
            m_handle_uvs.push_back(uv);
            ++packed;
        }
    }
    SYNCLOG("VKSpriteAtlas::AddSheet: packed %d/%ld sprites from '%s'", packed, n, label);
}

void VKSpriteAtlas::Rebuild(const struct TbSpriteSheet* const* sheets,
                             const char* const* names, int count)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ++m_generation;
    Free_Internal();
    if (!Init_Internal()) return;
    for (int i = 0; i < count; ++i) {
        if (sheets[i]) AddSheet_Internal(sheets[i], names ? names[i] : nullptr);
    }
}

void VKSpriteAtlas::RemoveSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    long n = num_sprites(sheet);
    for (long i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (spr) m_sprite_to_handle.erase(spr);
    }
}

/******************************************************************************/

SpriteHandle VKSpriteAtlas::GetHandle(const struct TbSprite* spr) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetHandle_Unlocked(spr);
}

SpriteHandle VKSpriteAtlas::GetHandle_Unlocked(const struct TbSprite* spr) const
{
    auto it = m_sprite_to_handle.find(spr);
    return (it != m_sprite_to_handle.end()) ? it->second : kInvalidSpriteHandle;
}

bool VKSpriteAtlas::GetUV(SpriteHandle h, SpriteUV& out) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetUV_Unlocked(h, out);
}

bool VKSpriteAtlas::GetUV_Unlocked(SpriteHandle h, SpriteUV& out) const
{
    if (h == kInvalidSpriteHandle) return false;
    const uint16_t gen   = SpriteHandleGeneration(h);
    const uint16_t index = SpriteHandleIndex(h);
    if (gen != m_generation || index >= m_handle_uvs.size()) return false;
    out = m_handle_uvs[index];
    return true;
}

bool VKSpriteAtlas::GetUV(const struct TbSprite* spr, SpriteUV& out) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetUV_Unlocked(GetHandle_Unlocked(spr), out);
}

size_t VKSpriteAtlas::GetRegisteredCount() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_handle_uvs.size();
}

/******************************************************************************/

/** Helper: insert a pipeline barrier to transition the atlas image layout. */
static void transition_image(VkCommandBuffer cmd, VkImage image,
                              VkImageLayout old_layout, VkImageLayout new_layout,
                              VkAccessFlags src_access,  VkPipelineStageFlags src_stage,
                              VkAccessFlags dst_access,  VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = old_layout;
    barrier.newLayout           = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = src_access;
    barrier.dstAccessMask       = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VKSpriteAtlas::FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    if (m_vk_init_needed && !m_pixels.empty()) {
        // ── Create VkImage ────────────────────────────────────────────────────
        VkImageCreateInfo img_info = {};
        img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType     = VK_IMAGE_TYPE_2D;
        img_info.format        = VK_FORMAT_R8_UNORM;
        img_info.extent        = { (uint32_t)k_atlas_w, (uint32_t)k_atlas_h, 1 };
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
            ERRORLOG("VKSpriteAtlas: vmaCreateImage failed");
            return;
        }

        // ── Create VkImageView ────────────────────────────────────────────────
        VkImageViewCreateInfo view_info = {};
        view_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image            = m_image;
        view_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format           = VK_FORMAT_R8_UNORM;
        view_info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &view_info, nullptr, &m_view) != VK_SUCCESS) {
            ERRORLOG("VKSpriteAtlas: vkCreateImageView failed");
            return;
        }

        // ── Create VkSampler ─────────────────────────────────────────────────
        VkSamplerCreateInfo samp_info = {};
        samp_info.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp_info.magFilter     = VK_FILTER_NEAREST;
        samp_info.minFilter     = VK_FILTER_NEAREST;
        samp_info.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp_info.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        if (vkCreateSampler(m_device, &samp_info, nullptr, &m_sampler) != VK_SUCCESS) {
            ERRORLOG("VKSpriteAtlas: vkCreateSampler failed");
            return;
        }

        // ── Upload full pixel buffer ──────────────────────────────────────────
        const size_t total = (size_t)k_atlas_w * k_atlas_h;
        VKStagingAlloc sa;
        if (!staging.Alloc(m_pixels.data(), (VkDeviceSize)total, 1, sa)) {
            ERRORLOG("VKSpriteAtlas: staging ring exhausted during full init upload");
            return;
        }

        transition_image(cmd, m_image,
            VK_IMAGE_LAYOUT_UNDEFINED,              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,                                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,           VK_PIPELINE_STAGE_TRANSFER_BIT);

        staging.CmdCopyToImage(cmd, sa.offset, sa.buffer,
                               m_image, k_atlas_w, k_atlas_h);

        transition_image(cmd, m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,           VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT,              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        m_vk_init_needed = false;
        m_dirty_y_min    = k_atlas_h;
        m_dirty_y_max    = -1;
        SYNCLOG("VKSpriteAtlas: %dx%d R8 atlas uploaded to GPU", k_atlas_w, k_atlas_h);
    }
    else if (m_image != VK_NULL_HANDLE && m_dirty_y_min <= m_dirty_y_max)
    {
        // ── Partial (incremental) upload ──────────────────────────────────────
        const int dirty_h = m_dirty_y_max - m_dirty_y_min;
        if (dirty_h <= 0) return;

        const size_t row_bytes = (size_t)k_atlas_w;
        const size_t upload_bytes = row_bytes * (size_t)dirty_h;
        const uint8_t* src = m_pixels.data() + (size_t)m_dirty_y_min * k_atlas_w;

        VKStagingAlloc sa;
        if (!staging.Alloc(src, (VkDeviceSize)upload_bytes, 1, sa)) {
            ERRORLOG("VKSpriteAtlas: staging ring exhausted during incremental upload");
            return;
        }

        transition_image(cmd, m_image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_SHADER_READ_BIT,                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,             VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region = {};
        region.bufferOffset                    = sa.offset;
        region.bufferRowLength                 = 0;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset                     = { 0, m_dirty_y_min, 0 };
        region.imageExtent                     = { (uint32_t)k_atlas_w, (uint32_t)dirty_h, 1 };
        vkCmdCopyBufferToImage(cmd, sa.buffer, m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transition_image(cmd, m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,             VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT,                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        m_dirty_y_min = k_atlas_h;
        m_dirty_y_max = -1;
    }
}

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
