/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKDescriptorLayout.cpp
 *     VkDescriptorPool management — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/vulkan/VKDescriptorLayout.h"
#include "globals.h"

#include "post_inc.h"

/******************************************************************************/

bool VKDescriptorLayout::Init(VkDevice device, VkDescriptorSetLayout set_layout,
                                uint32_t max_sets_per_frame, int frames_in_flight)
{
    m_device          = device;
    m_set_layout      = set_layout;
    m_frames_in_flight = frames_in_flight;

    // Each pool holds up to max_sets_per_frame combined-image-sampler descriptors.
    // We have 4 bindings per set layout, so each set consumes up to 4 descriptors.
    VkDescriptorPoolSize pool_size = {};
    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = max_sets_per_frame * 4;

    VkDescriptorPoolCreateInfo pool_ci = {};
    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.maxSets       = max_sets_per_frame;
    pool_ci.poolSizeCount = 1;
    pool_ci.pPoolSizes    = &pool_size;
    // No FREE_DESCRIPTOR_SET flag — reset entire pool each frame for O(1) reset.

    m_pools.resize((size_t)frames_in_flight, VK_NULL_HANDLE);
    for (int i = 0; i < frames_in_flight; ++i)
    {
        if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &m_pools[i]) != VK_SUCCESS)
        {
            ERRORLOG("VKDescriptorLayout: failed to create descriptor pool %d", i);
            return false;
        }
    }

    SYNCLOG("VKDescriptorLayout: created %d pools x %u sets", frames_in_flight, max_sets_per_frame);
    return true;
}

void VKDescriptorLayout::BeginFrame(int frame_index)
{
    m_current_frame = frame_index % m_frames_in_flight;
    vkResetDescriptorPool(m_device, m_pools[m_current_frame], 0);
}

VkDescriptorSet VKDescriptorLayout::Alloc()
{
    VkDescriptorSetAllocateInfo alloc_ci = {};
    alloc_ci.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_ci.descriptorPool     = m_pools[m_current_frame];
    alloc_ci.descriptorSetCount = 1;
    alloc_ci.pSetLayouts        = &m_set_layout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device, &alloc_ci, &set);
    if (result != VK_SUCCESS)
    {
        ERRORLOG("VKDescriptorLayout: descriptor pool exhausted (frame %d)", m_current_frame);
        return VK_NULL_HANDLE;
    }
    return set;
}

void VKDescriptorLayout::UpdateCombinedImageSampler(VkDescriptorSet set, uint32_t binding,
                                                      VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo img_info = {};
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    img_info.imageView   = view;
    img_info.sampler     = sampler;

    VkWriteDescriptorSet write = {};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = binding;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &img_info;

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void VKDescriptorLayout::UpdateCombinedImageSamplers(VkDescriptorSet set, uint32_t count,
                                                      const VkImageView* views,
                                                      const VkSampler* samplers)
{
    VkDescriptorImageInfo img_infos[4] = {};
    VkWriteDescriptorSet  writes[4]    = {};

    for (uint32_t i = 0; i < count && i < 4; ++i)
    {
        img_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_infos[i].imageView   = views[i];
        img_infos[i].sampler     = samplers[i];

        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = set;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo      = &img_infos[i];
    }

    vkUpdateDescriptorSets(m_device, count, writes, 0, nullptr);
}

void VKDescriptorLayout::Shutdown()
{
    for (VkDescriptorPool pool : m_pools)
    {
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_device, pool, nullptr);
    }
    m_pools.clear();
}

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
