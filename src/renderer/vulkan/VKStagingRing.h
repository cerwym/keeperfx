/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKStagingRing.h
 *     Per-frame CPU→GPU upload ring buffer.
 * @par Purpose:
 *     Replaces GL's glBufferSubData pattern.  One large persistently-mapped
 *     host-visible buffer is divided into kVKFramesInFlight equal windows.
 *     Each frame writes into its window via Alloc(); vkCmdCopyBuffer* transfers
 *     to device-local targets.  The fence in VKDevice::AcquireNextImage ensures
 *     the window is not in use by the GPU before we write into it again.
 */
/******************************************************************************/
#pragma once
#ifndef VKSTAGINGRING_H
#define VKSTAGINGRING_H

#ifdef RENDERER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <cstddef>
#include <cstdint>

/******************************************************************************/

/** Result from a single Alloc() call. */
struct VKStagingAlloc
{
    void*        cpu_ptr = nullptr;  // Mapped host pointer — write here
    VkBuffer     buffer  = VK_NULL_HANDLE;
    VkDeviceSize offset  = 0;
};

/**
 * Per-frame CPU→GPU upload ring buffer backed by a VMA host-visible allocation.
 *
 * Lifecycle:
 *   Init(device, allocator, total_bytes)  — called once at renderer startup
 *   BeginFrame(frame_index)               — selects the current frame's window
 *   Alloc(data, size, alignment)          — copies data into the ring, returns handles
 *   Shutdown()                            — destroys the VMA buffer
 *
 * Thread safety: all methods must be called from the render thread.
 */
class VKStagingRing
{
public:
    VKStagingRing() = default;
    VKStagingRing(const VKStagingRing&) = delete;
    VKStagingRing& operator=(const VKStagingRing&) = delete;

    /** Create the ring buffer.
     *  @param device     Logical device.
     *  @param allocator  VmaAllocator owning the allocation.
     *  @param total_bytes Total buffer size; each frame window = total_bytes / kVKFramesInFlight.
     *  @return true on success. */
    bool Init(VkDevice device, VmaAllocator allocator, VkDeviceSize total_bytes);

    /** Select the write window for this frame.
     *  @param frame_index  0 or 1 (kVKFramesInFlight). */
    void BeginFrame(int frame_index);

    /** Allocate @p size bytes in the current window, copy @p data there.
     *  @param data       Source bytes (may be nullptr — returns pointer without copying).
     *  @param size       Byte count to reserve.
     *  @param alignment  Required VkDeviceSize alignment (e.g. 4 for index buffers).
     *  @param out        Receives the mapping pointer, VkBuffer and byte offset.
     *  @return false if the window is exhausted; the caller must flush before allocating. */
    bool Alloc(const void* data, VkDeviceSize size, VkDeviceSize alignment, VKStagingAlloc& out);

    /** Record a buffer→buffer copy into @p cmd from staging offset @p src_off
     *  to @p dst_buf at @p dst_off for @p size bytes. */
    void CmdCopyToBuffer(VkCommandBuffer cmd,
                         VkDeviceSize src_off, VkDeviceSize size,
                         VkBuffer dst_buf, VkDeviceSize dst_off) const;

    /** Record a buffer→image copy into @p cmd from staging offset @p src_off
     *  to @p dst_image (must be in TRANSFER_DST_OPTIMAL layout).
     *  The region covers the entire mip 0, layer 0 of a @p w × @p h × 1 image. */
    void CmdCopyToImage(VkCommandBuffer cmd,
                        VkDeviceSize src_off, VkBuffer staging_buf,
                        VkImage dst_image, uint32_t w, uint32_t h) const;

    void Shutdown();

    VkBuffer GetBuffer() const { return m_buffer; }

private:
    VkDevice      m_device    = VK_NULL_HANDLE;
    VmaAllocator  m_allocator = VK_NULL_HANDLE;
    VkBuffer      m_buffer    = VK_NULL_HANDLE;
    VmaAllocation m_alloc     = VK_NULL_HANDLE;
    uint8_t*      m_mapped    = nullptr;

    VkDeviceSize  m_window_size    = 0;  // bytes per frame window
    VkDeviceSize  m_window_base    = 0;  // start of current frame's window
    VkDeviceSize  m_write_cursor   = 0;  // current write position within window
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
#endif // VKSTAGINGRING_H
