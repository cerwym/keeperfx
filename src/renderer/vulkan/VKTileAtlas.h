/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKTileAtlas.h
 *     Vulkan tile texture atlas (2D image array, R8_UNORM).
 * @par Purpose:
 *     Mirrors GLTileAtlas but uses a VkImage (2D array, all variations as
 *     layers) instead of GL_TEXTURE_2D_ARRAY.  Inherits TileAtlasPacker for
 *     the shared CPU-side pixel decode + UV helpers.
 *
 *     FlushPendingVK() uploads all dirty variation layers via VKStagingRing.
 *     Must be called on the render thread before the first tile draw.
 */
/******************************************************************************/
#pragma once
#ifndef VKTILEATLAS_H
#define VKTILEATLAS_H

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include "renderer/TileAtlasPacker.h"
#include "renderer/ITileAtlas.h"
#include "renderer/vulkan/VKStagingRing.h"

/******************************************************************************/

class VKTileAtlas : public TileAtlasPacker, public ITileAtlas
{
public:
    VKTileAtlas() = default;
    ~VKTileAtlas() override { Free(); }

    VKTileAtlas(const VKTileAtlas&)            = delete;
    VKTileAtlas& operator=(const VKTileAtlas&) = delete;

    /** Set device / allocator — call before Init(). */
    void SetDevice(VkDevice device, VmaAllocator allocator);

    // ITileAtlas
    bool         Init() override;
    void         Free() override;
    void         UpdateAnimatedTiles() override;
    unsigned int GetAtlasTexture(int variation) const override;
    unsigned int GetAtlasTextureArray() const override { return 0; }

    /** Upload all dirty variation layers to GPU via the staging ring. */
    void FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging);

    VkImageView GetView()    const { return m_view;    }
    VkSampler   GetSampler() const { return m_sampler; }

protected:
    // TileAtlasPacker overrides — store R8 (single channel) instead of RGBA8.
    void BuildVariation(int variation) override;
    void BuildAnimatedStrip(int variation) override;

    void UploadFull(int variation) override;
    void UploadAnimatedStrip(int variation, int y_offset, int h_pixels) override;

private:
    VkDevice      m_device    = VK_NULL_HANDLE;
    VmaAllocator  m_allocator = VK_NULL_HANDLE;

    VkImage       m_image       = VK_NULL_HANDLE;
    VmaAllocation m_image_alloc = VK_NULL_HANDLE;
    VkImageView   m_view        = VK_NULL_HANDLE;
    VkSampler     m_sampler     = VK_NULL_HANDLE;

    // Staged CPU R8 scratch per variation (k_atlas_w × k_atlas_h, R8)
    uint8_t* m_r8_scratch = nullptr;

    // Per-variation dirty flags set by BuildVariation/BuildAnimatedStrip
    bool m_dirty[k_max_variations]       = {};
    int  m_dirty_y_min[k_max_variations] = {};
    int  m_dirty_y_max[k_max_variations] = {};
    bool m_image_needs_init              = true;

    // Per-variation: CPU R8 backing (k_atlas_w × k_atlas_h per layer)
    uint8_t* m_cpu_pixels[k_max_variations] = {};
};

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
#endif // VKTILEATLAS_H
