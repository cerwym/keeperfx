/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKSpriteAtlas.h
 *     Vulkan sprite texture atlas.
 * @par Purpose:
 *     Mirrors GLSpriteAtlas but owns a VkImage instead of a GL texture.
 *     Packs decoded TbSprite pixel data (palette indices) into a single
 *     4096×2048 VK_FORMAT_R8_UNORM image using a shelf packer.  Keyed by
 *     TbSprite* so VKUIRenderer can look up UV coordinates in O(1).
 *
 *     FlushPendingVK() performs the staged GPU upload: it allocates space in
 *     the staging ring, copies pixel data, transitions the image layout, and
 *     records the copy command.  Must be called on the render thread with an
 *     active command buffer before any draw calls that sample this atlas.
 */
/******************************************************************************/
#pragma once
#ifndef VKSPRITEATLAS_H
#define VKSPRITEATLAS_H

#ifdef RENDERER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "renderer/SpriteHandle.h"
#include "renderer/opengl/GLSpriteAtlas.h"  // SpriteUV, shared struct

struct TbSprite;
struct TbSpriteSheet;

class VKStagingRing;

/******************************************************************************/

class VKSpriteAtlas
{
public:
    static const int k_atlas_w = 4096;
    static const int k_atlas_h = 2048;

    VKSpriteAtlas() = default;
    ~VKSpriteAtlas() { Free(); }

    VKSpriteAtlas(const VKSpriteAtlas&)            = delete;
    VKSpriteAtlas& operator=(const VKSpriteAtlas&) = delete;

    /** Set device / allocator — must be called once before Init(). */
    void SetDevice(VkDevice device, VmaAllocator allocator,
                   VkCommandPool cmd_pool, VkQueue transfer_queue);

    bool Init();
    void Free();

    /** Decode and pack all sprites in a sheet into the atlas.
     *  Thread-safe: takes exclusive lock. */
    void AddSheet(const struct TbSpriteSheet* sheet, const char* name = nullptr);

    /** Atomically rebuild the entire atlas. */
    void Rebuild(const struct TbSpriteSheet* const* sheets,
                 const char* const* names, int count);

    /** Invalidate UV entries for all sprites in a sheet. */
    void RemoveSheet(const struct TbSpriteSheet* sheet);

    /** Look up sprite handle.  O(1). */
    SpriteHandle GetHandle(const struct TbSprite* spr) const;

    /** Look up UV coordinates by handle. */
    bool GetUV(SpriteHandle h, SpriteUV& out) const;

    /** Look up UV coordinates by sprite pointer. */
    bool GetUV(const struct TbSprite* spr, SpriteUV& out) const;

    /** Upload dirty pixels to GPU via the staging ring.
     *  @param cmd      Recording command buffer.
     *  @param staging  Per-frame staging ring for uploads.
     *  Records layout transitions (UNDEFINED → TRANSFER_DST → SHADER_READ).
     *  Must be called on the render thread before the first draw that samples
     *  this atlas. */
    void FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging);

    VkImageView GetView()    const { return m_view;    }
    VkSampler   GetSampler() const { return m_sampler; }

    size_t GetRegisteredCount() const;

private:
    bool pack_sprite(const struct TbSprite* spr, SpriteUV& out);
    void decode_rle(uint8_t* dst, int dst_stride, const struct TbSprite* spr);

    SpriteHandle GetHandle_Unlocked(const struct TbSprite* spr) const;
    bool         GetUV_Unlocked(SpriteHandle h, SpriteUV& out) const;
    void         Free_Internal();
    bool         Init_Internal();
    void         AddSheet_Internal(const struct TbSpriteSheet* sheet, const char* name);

    mutable std::shared_mutex m_mutex;

    VkDevice      m_device      = VK_NULL_HANDLE;
    VmaAllocator  m_allocator   = VK_NULL_HANDLE;
    VkCommandPool m_cmd_pool    = VK_NULL_HANDLE;
    VkQueue       m_queue       = VK_NULL_HANDLE;

    VkImage       m_image       = VK_NULL_HANDLE;
    VmaAllocation m_image_alloc = VK_NULL_HANDLE;
    VkImageView   m_view        = VK_NULL_HANDLE;
    VkSampler     m_sampler     = VK_NULL_HANDLE;

    bool                 m_vk_init_needed = false;  // set by Init(); cleared by FlushPendingVK()
    std::vector<uint8_t> m_pixels;                  // CPU copy, k_atlas_w × k_atlas_h

    // Shelf packer state
    int m_cursor_x = 0;
    int m_shelf_y  = 0;
    int m_shelf_h  = 0;

    // Dirty region for partial upload
    int m_dirty_y_min = k_atlas_h;
    int m_dirty_y_max = -1;

    std::unordered_map<const struct TbSprite*, SpriteHandle> m_sprite_to_handle;
    std::vector<SpriteUV>                                    m_handle_uvs;
    uint32_t                                                 m_next_handle = 0;
    uint16_t                                                 m_generation  = 0;
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
#endif // VKSPRITEATLAS_H
