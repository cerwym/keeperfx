/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKFontAtlas.h
 *     Vulkan font texture atlas.
 * @par Purpose:
 *     Mirrors GLFontAtlas but owns a VkImage (VK_FORMAT_R8G8B8A8_UNORM) instead
 *     of a GL texture.  Built from a TbSpriteSheet font sheet; provides per-glyph
 *     UV coordinates for VKUIRenderer and VKTextRenderer.
 */
/******************************************************************************/
#pragma once
#ifndef VKFONTATLAS_H
#define VKFONTATLAS_H

#ifdef RENDERER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <cstdint>

struct TbSpriteSheet;
class VKStagingRing;

/** Glyph data (same layout as GLFontAtlas::FontGlyph). */
struct VKFontGlyph {
    float u0, v0, u1, v1;
    int   width, height;
};

/******************************************************************************/

class VKFontAtlas
{
public:
    VKFontAtlas() = default;
    ~VKFontAtlas() { Shutdown(); }

    VKFontAtlas(const VKFontAtlas&)            = delete;
    VKFontAtlas& operator=(const VKFontAtlas&) = delete;

    /** Set device / allocator — must be called once before Init(). */
    void SetDevice(VkDevice device, VmaAllocator allocator);

    /** Build CPU glyph data from the font sheet.
     *  No GPU work is done here; call FlushPendingVK() on the render thread. */
    bool Init(const struct TbSpriteSheet* font_sheet);

    /** Free all GPU resources. */
    void Shutdown();

    /** Upload the font atlas pixels to GPU via the staging ring.
     *  Must be called from the render thread with an active command buffer.
     *  No-op if already uploaded and not dirty. */
    void FlushPendingVK(VkCommandBuffer cmd, VKStagingRing& staging);

    const VKFontGlyph* GetGlyph(unsigned long chr) const;
    int  GetLineHeight()    const { return m_line_height;    }
    int  GetMaxCharWidth()  const { return m_max_char_width; }
    bool IsInitialized()    const { return m_view != VK_NULL_HANDLE; }
    bool NeedsRebuild(const struct TbSpriteSheet* font_sheet) const;

    VkImageView GetView()    const { return m_view;    }
    VkSampler   GetSampler() const { return m_sampler; }

    int GetAtlasWidth()  const { return m_atlas_width;  }
    int GetAtlasHeight() const { return m_atlas_height; }

private:
    VkDevice      m_device    = VK_NULL_HANDLE;
    VmaAllocator  m_allocator = VK_NULL_HANDLE;

    VkImage       m_image       = VK_NULL_HANDLE;
    VmaAllocation m_image_alloc = VK_NULL_HANDLE;
    VkImageView   m_view        = VK_NULL_HANDLE;
    VkSampler     m_sampler     = VK_NULL_HANDLE;

    bool                  m_vk_init_needed = false;
    std::vector<uint8_t>  m_pixels;               // RGBA8 CPU copy
    std::vector<VKFontGlyph> m_glyphs;            // 256 entries
    int  m_line_height    = 0;
    int  m_max_char_width = 0;
    int  m_atlas_width    = 0;
    int  m_atlas_height   = 0;
    long m_sprite_count   = 0;                    // for NeedsRebuild()
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
#endif // VKFONTATLAS_H
