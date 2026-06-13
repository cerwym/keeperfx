/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKStagingRing.cpp
 *     Per-frame CPU→GPU upload ring buffer — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/vulkan/VKStagingRing.h"
#include "renderer/vulkan/VKDevice.h"
#include "globals.h"

#include "post_inc.h"

/******************************************************************************/

bool VKStagingRing::Init(VkDevice device, VmaAllocator allocator, VkDeviceSize total_bytes)
{
    m_device    = device;
    m_allocator = allocator;
    m_window_size = (total_bytes + kVKFramesInFlight - 1) / kVKFramesInFlight;
    // Align window size to 256 bytes so per-frame offsets stay aligned.
    m_window_size = (m_window_size + 255) & ~(VkDeviceSize)255;
    VkDeviceSize actual_size = m_window_size * kVKFramesInFlight;

    VkBufferCreateInfo buf_ci = {};
    buf_ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size        = actual_size;
    buf_ci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    if (vmaCreateBuffer(m_allocator, &buf_ci, &alloc_ci,
                        &m_buffer, &m_alloc, &alloc_info) != VK_SUCCESS)
    {
        ERRORLOG("VKStagingRing: failed to create staging buffer (%llu bytes)", (unsigned long long)actual_size);
        return false;
    }

    m_mapped = static_cast<uint8_t*>(alloc_info.pMappedData);
    if (!m_mapped)
    {
        ERRORLOG("VKStagingRing: staging buffer not persistently mapped");
        return false;
    }

    SYNCLOG("VKStagingRing: created %llu byte ring (%llu per frame)",
            (unsigned long long)actual_size, (unsigned long long)m_window_size);
    return true;
}

void VKStagingRing::BeginFrame(int frame_index)
{
    m_window_base  = (VkDeviceSize)frame_index * m_window_size;
    m_write_cursor = 0;
}

bool VKStagingRing::Alloc(const void* data, VkDeviceSize size,
                           VkDeviceSize alignment, VKStagingAlloc& out)
{
    // Align cursor within window.
    VkDeviceSize cursor = (m_write_cursor + alignment - 1) & ~(alignment - 1);
    if (cursor + size > m_window_size)
    {
        ERRORLOG("VKStagingRing: staging window exhausted (need %llu, have %llu)",
                 (unsigned long long)(cursor + size), (unsigned long long)m_window_size);
        return false;
    }

    VkDeviceSize abs_offset = m_window_base + cursor;
    if (data)
        memcpy(m_mapped + abs_offset, data, (size_t)size);

    out.cpu_ptr = m_mapped + abs_offset;
    out.buffer  = m_buffer;
    out.offset  = abs_offset;

    m_write_cursor = cursor + size;
    return true;
}

void VKStagingRing::CmdCopyToBuffer(VkCommandBuffer cmd,
                                     VkDeviceSize src_off, VkDeviceSize size,
                                     VkBuffer dst_buf, VkDeviceSize dst_off) const
{
    VkBufferCopy region = {};
    region.srcOffset = src_off;
    region.dstOffset = dst_off;
    region.size      = size;
    vkCmdCopyBuffer(cmd, m_buffer, dst_buf, 1, &region);
}

void VKStagingRing::CmdCopyToImage(VkCommandBuffer cmd,
                                    VkDeviceSize src_off, VkBuffer staging_buf,
                                    VkImage dst_image, uint32_t w, uint32_t h) const
{
    VkBufferImageCopy region = {};
    region.bufferOffset                    = src_off;
    region.bufferRowLength                 = 0;  // tightly packed
    region.bufferImageHeight               = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset                     = {0, 0, 0};
    region.imageExtent                     = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging_buf, dst_image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VKStagingRing::Shutdown()
{
    if (m_buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(m_allocator, m_buffer, m_alloc);
        m_buffer = VK_NULL_HANDLE;
        m_alloc  = VK_NULL_HANDLE;
        m_mapped = nullptr;
    }
}

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
