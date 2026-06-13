/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKDescriptorLayout.h
 *     VkDescriptorPool management and per-frame descriptor set allocation.
 *
 * VKUIRenderer and VKWorldViewRenderer allocate VkDescriptorSet handles from
 * this pool each frame and update them with the textures/samplers they need.
 * The pool is reset once per frame (VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
 * is NOT set) so allocation is O(1) and there is no per-set freeing overhead.
 */
/******************************************************************************/
#pragma once
#ifndef VKDESCRIPTORLAYOUT_H
#define VKDESCRIPTORLAYOUT_H

#ifdef RENDERER_VULKAN_ENABLED

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

/******************************************************************************/

/**
 * Per-frame descriptor pool manager.
 *
 * Usage:
 *   Init(device, set_layout, max_sets_per_frame, frames_in_flight)
 *   — once at startup.
 *
 *   BeginFrame(frame_index)
 *   — call before any Alloc() for this frame.  Resets the per-frame pool.
 *
 *   Alloc()
 *   — returns one VkDescriptorSet allocated from the current frame's pool.
 *     The set layout matches the one passed to Init().
 *
 *   UpdateCombinedImageSampler(set, binding, view, sampler)
 *   — convenience: writes a single combined-image-sampler descriptor.
 */
class VKDescriptorLayout
{
public:
    VKDescriptorLayout() = default;
    VKDescriptorLayout(const VKDescriptorLayout&) = delete;
    VKDescriptorLayout& operator=(const VKDescriptorLayout&) = delete;

    /** Create pools (one per frame in flight).
     *  @param device               Logical device.
     *  @param set_layout           Descriptor set layout to allocate sets from.
     *  @param max_sets_per_frame   Maximum sets per frame; pool is pre-sized to this.
     *  @param frames_in_flight     Number of concurrent frames (1 or 2).
     *  @return true on success. */
    bool Init(VkDevice device, VkDescriptorSetLayout set_layout,
              uint32_t max_sets_per_frame, int frames_in_flight);

    /** Reset the current frame's pool and prepare for new allocations.
     *  @param frame_index  0-based frame index mod frames_in_flight. */
    void BeginFrame(int frame_index);

    /** Allocate one VkDescriptorSet from the current frame's pool.
     *  @return VK_NULL_HANDLE on exhaustion (increase max_sets_per_frame). */
    VkDescriptorSet Alloc();

    /** Write a combined-image-sampler into @p set at @p binding. */
    void UpdateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
                                     VkImageView view, VkSampler sampler);

    /** Write N combined-image-samplers into @p set starting at binding 0. */
    void UpdateCombinedImageSamplers(VkDescriptorSet set, uint32_t count,
                                      const VkImageView* views,
                                      const VkSampler* samplers);

    void Shutdown();

    bool IsReady() const { return !m_pools.empty(); }

private:
    VkDevice              m_device     = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> m_pools;  // one per frame in flight
    int m_current_frame = 0;
    int m_frames_in_flight = 0;
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
#endif // VKDESCRIPTORLAYOUT_H
