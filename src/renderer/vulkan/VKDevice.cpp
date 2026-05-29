/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKDevice.cpp
 *     Vulkan instance / device / swapchain RAII wrapper — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/vulkan/VKDevice.h"
#include "globals.h"

// vk-bootstrap — single-header bootstrap library
#include <VkBootstrap.h>

#ifdef RENDERER_VULKAN_ENABLED
// VMA implementation — compiled exactly once here
#  define VMA_IMPLEMENTATION
#  include <vma/vk_mem_alloc.h>
#endif

#include "post_inc.h"

/******************************************************************************/

bool VKDevice::InitInstance()
{
    vkb::InstanceBuilder builder;
    builder.set_app_name("KeeperFX")
           .require_api_version(1, 1, 0);

#ifdef _DEBUG
    builder.request_validation_layers()
           .use_default_debug_messenger();
#endif

    auto inst_ret = builder.build();
    if (!inst_ret)
    {
        ERRORLOG("VKDevice: failed to create Vulkan instance: %s", inst_ret.error().message().c_str());
        return false;
    }

    vkb::Instance vkb_inst = inst_ret.value();
    m_instance        = vkb_inst.instance;
    m_debug_messenger = vkb_inst.debug_messenger;
    return true;
}

bool VKDevice::InitDevice(VkSurfaceKHR surface, int w, int h)
{
    m_surface = surface;

    // --- Physical device ---
    vkb::PhysicalDeviceSelector phys_sel{vkb::Instance{}}; // placeholder; we use raw instance below
    // vk-bootstrap PhysicalDeviceSelector takes a vkb::Instance which it uses
    // to call vkEnumeratePhysicalDevices.  Reconstruct a minimal vkb::Instance
    // struct from our stored handles so the selector can use them.
    vkb::Instance vkb_inst;
    vkb_inst.instance        = m_instance;
    vkb_inst.debug_messenger = m_debug_messenger;

    vkb::PhysicalDeviceSelector real_sel{vkb_inst};
    real_sel.set_minimum_version(1, 1);
    if (m_surface != VK_NULL_HANDLE)
        real_sel.set_surface(m_surface);

    auto phys_ret = real_sel.select();
    if (!phys_ret)
    {
        ERRORLOG("VKDevice: no suitable GPU found: %s", phys_ret.error().message().c_str());
        return false;
    }

    // --- Logical device ---
    vkb::DeviceBuilder dev_builder{phys_ret.value()};
    auto dev_ret = dev_builder.build();
    if (!dev_ret)
    {
        ERRORLOG("VKDevice: failed to create logical device: %s", dev_ret.error().message().c_str());
        return false;
    }

    vkb::Device vkb_dev = dev_ret.value();
    m_physical_device = vkb_dev.physical_device;
    m_device          = vkb_dev.device;

    // Queues
    auto gq = vkb_dev.get_queue(vkb::QueueType::graphics);
    auto pq = vkb_dev.get_queue(vkb::QueueType::present);
    if (!gq || !pq)
    {
        ERRORLOG("VKDevice: failed to obtain graphics/present queues");
        return false;
    }
    m_graphics_queue       = gq.value();
    m_graphics_queue_index = vkb_dev.get_queue_index(vkb::QueueType::graphics).value();
    m_present_queue        = pq.value();
    m_present_queue_index  = vkb_dev.get_queue_index(vkb::QueueType::present).value();

#ifdef RENDERER_VULKAN_ENABLED
    // --- VMA allocator ---
    if (!CreateAllocator())
        return false;
#endif

    // --- Render pass ---
    if (!CreateRenderPass())
        return false;

    // --- Command pool ---
    VkCommandPoolCreateInfo pool_ci = {};
    pool_ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = m_graphics_queue_index;
    pool_ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &pool_ci, nullptr, &m_command_pool) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to create command pool");
        return false;
    }

    // --- Command buffers ---
    VkCommandBufferAllocateInfo alloc_ci = {};
    alloc_ci.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_ci.commandPool        = m_command_pool;
    alloc_ci.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = kVKFramesInFlight;
    if (vkAllocateCommandBuffers(m_device, &alloc_ci, m_cmd_bufs) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to allocate command buffers");
        return false;
    }

    // --- Sync objects ---
    if (!CreateSyncObjects())
        return false;

    // --- Swapchain (only if we have a surface) ---
    if (m_surface != VK_NULL_HANDLE)
    {
        if (!CreateSwapchain(w, h))
            return false;
    }

    SYNCLOG("VKDevice: initialised (%dx%d)", w, h);
    return true;
}

void VKDevice::Shutdown()
{
    if (m_instance == VK_NULL_HANDLE)
        return;

    if (m_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(m_device);

#ifdef RENDERER_VULKAN_ENABLED
    if (m_allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
#endif

    DestroySwapchain();
    DestroySyncObjects();

    if (m_command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_device, m_command_pool, nullptr);
        m_command_pool = VK_NULL_HANDLE;
    }

    if (m_render_pass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(m_device, m_render_pass, nullptr);
        m_render_pass = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    // Surface must be destroyed before the instance.
    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_debug_messenger != VK_NULL_HANDLE)
    {
        vkb::destroy_debug_utils_messenger(m_instance, m_debug_messenger);
        m_debug_messenger = VK_NULL_HANDLE;
    }

    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
}

/******************************************************************************/

bool VKDevice::CreateRenderPass()
{
    VkAttachmentDescription colour_att = {};
    colour_att.format         = VK_FORMAT_B8G8R8A8_UNORM; // overwritten on swapchain create
    colour_att.samples        = VK_SAMPLE_COUNT_1_BIT;
    colour_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colour_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colour_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colour_att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colour_ref = {};
    colour_ref.attachment = 0;
    colour_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colour_ref;

    VkSubpassDependency dep = {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_ci = {};
    rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_ci.attachmentCount = 1;
    rp_ci.pAttachments    = &colour_att;
    rp_ci.subpassCount    = 1;
    rp_ci.pSubpasses      = &subpass;
    rp_ci.dependencyCount = 1;
    rp_ci.pDependencies   = &dep;

    if (vkCreateRenderPass(m_device, &rp_ci, nullptr, &m_render_pass) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to create render pass");
        return false;
    }
    return true;
}

bool VKDevice::CreateSwapchain(int w, int h)
{
    vkb::SwapchainBuilder sc_builder{m_physical_device, m_device, m_surface,
                                     (uint32_t)m_graphics_queue_index,
                                     (uint32_t)m_present_queue_index};

    sc_builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
              .set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR)
              .add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR)
              .set_desired_extent((uint32_t)w, (uint32_t)h)
              .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    auto sc_ret = sc_builder.build();
    if (!sc_ret)
    {
        ERRORLOG("VKDevice: failed to create swapchain: %s", sc_ret.error().message().c_str());
        return false;
    }

    vkb::Swapchain vkb_sc = sc_ret.value();
    m_swapchain        = vkb_sc.swapchain;
    m_swapchain_format = vkb_sc.image_format;
    m_extent           = vkb_sc.extent;
    m_swapchain_images = vkb_sc.get_images().value();
    m_swapchain_image_views = vkb_sc.get_image_views().value();

    // Update render pass attachment format to match actual swapchain format.
    // We recreate the render pass here if the format differs; on first call
    // the render pass was created with B8G8R8A8_UNORM which matches the
    // requested format above.  If the driver chose something else, recreate.
    if (m_swapchain_format != VK_FORMAT_B8G8R8A8_UNORM && m_render_pass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(m_device, m_render_pass, nullptr);
        m_render_pass = VK_NULL_HANDLE;
        // Re-create with the actual format
        VkAttachmentDescription colour_att = {};
        colour_att.format         = m_swapchain_format;
        colour_att.samples        = VK_SAMPLE_COUNT_1_BIT;
        colour_att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colour_att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colour_att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colour_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colour_att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colour_att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colour_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colour_ref;

        VkSubpassDependency dep = {};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp_ci = {};
        rp_ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp_ci.attachmentCount = 1;
        rp_ci.pAttachments    = &colour_att;
        rp_ci.subpassCount    = 1;
        rp_ci.pSubpasses      = &subpass;
        rp_ci.dependencyCount = 1;
        rp_ci.pDependencies   = &dep;
        vkCreateRenderPass(m_device, &rp_ci, nullptr, &m_render_pass);
    }

    return CreateFramebuffers();
}

void VKDevice::DestroySwapchain()
{
    for (VkFramebuffer fb : m_framebuffers)
        vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    for (VkImageView iv : m_swapchain_image_views)
        vkDestroyImageView(m_device, iv, nullptr);
    m_swapchain_image_views.clear();
    m_swapchain_images.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VKDevice::CreateFramebuffers()
{
    m_framebuffers.resize(m_swapchain_image_views.size());
    for (size_t i = 0; i < m_swapchain_image_views.size(); ++i)
    {
        VkFramebufferCreateInfo fb_ci = {};
        fb_ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass      = m_render_pass;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments    = &m_swapchain_image_views[i];
        fb_ci.width           = m_extent.width;
        fb_ci.height          = m_extent.height;
        fb_ci.layers          = 1;

        if (vkCreateFramebuffer(m_device, &fb_ci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
        {
            ERRORLOG("VKDevice: failed to create framebuffer %zu", i);
            return false;
        }
    }
    return true;
}

bool VKDevice::CreateSyncObjects()
{
    VkSemaphoreCreateInfo sem_ci = {};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_ci = {};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kVKFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(m_device, &sem_ci, nullptr, &m_image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &sem_ci, nullptr, &m_render_finished[i]) != VK_SUCCESS ||
            vkCreateFence    (m_device, &fence_ci, nullptr, &m_in_flight_fences[i]) != VK_SUCCESS)
        {
            ERRORLOG("VKDevice: failed to create sync objects for frame %d", i);
            return false;
        }
    }
    return true;
}

void VKDevice::DestroySyncObjects()
{
    for (int i = 0; i < kVKFramesInFlight; ++i)
    {
        if (m_image_available[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_image_available[i], nullptr);
        if (m_render_finished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_render_finished[i], nullptr);
        if (m_in_flight_fences[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_device, m_in_flight_fences[i], nullptr);

        m_image_available[i]  = VK_NULL_HANDLE;
        m_render_finished[i]  = VK_NULL_HANDLE;
        m_in_flight_fences[i] = VK_NULL_HANDLE;
    }
}

/******************************************************************************/

VkResult VKDevice::AcquireNextImage(uint32_t& out_image_index)
{
    vkWaitForFences(m_device, 1, &m_in_flight_fences[m_current_frame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        m_image_available[m_current_frame], VK_NULL_HANDLE,
        &out_image_index);

    return result;
}

bool VKDevice::BeginFrame(VkCommandBuffer& out_cmd)
{
    vkResetFences(m_device, 1, &m_in_flight_fences[m_current_frame]);

    VkCommandBuffer cmd = m_cmd_bufs[m_current_frame];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_ci = {};
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cmd, &begin_ci) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to begin command buffer");
        return false;
    }

    out_cmd = cmd;
    return true;
}

VkResult VKDevice::SubmitAndPresent(VkCommandBuffer cmd, uint32_t image_index)
{
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to end command buffer");
        return VK_ERROR_UNKNOWN;
    }

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit_info = {};
    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount   = 1;
    submit_info.pWaitSemaphores      = &m_image_available[m_current_frame];
    submit_info.pWaitDstStageMask    = &wait_stage;
    submit_info.commandBufferCount   = 1;
    submit_info.pCommandBuffers      = &cmd;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores    = &m_render_finished[m_current_frame];

    if (vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_in_flight_fences[m_current_frame]) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: queue submit failed");
        return VK_ERROR_UNKNOWN;
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores    = &m_render_finished[m_current_frame];
    present_info.swapchainCount     = 1;
    present_info.pSwapchains        = &m_swapchain;
    present_info.pImageIndices      = &image_index;

    VkResult result = vkQueuePresentKHR(m_present_queue, &present_info);

    m_current_frame = (m_current_frame + 1) % kVKFramesInFlight;
    return result;
}

void VKDevice::RecreateSwapchain(int w, int h)
{
    if (m_device == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE)
        return;
    vkDeviceWaitIdle(m_device);
    DestroySwapchain();
    CreateSwapchain(w, h);
    SYNCLOG("VKDevice: swapchain recreated (%dx%d)", w, h);
}

VkFramebuffer VKDevice::GetFramebuffer(uint32_t index) const
{
    if (index < m_framebuffers.size())
        return m_framebuffers[index];
    return VK_NULL_HANDLE;
}

#ifdef RENDERER_VULKAN_ENABLED
bool VKDevice::CreateAllocator()
{
    VmaAllocatorCreateInfo alloc_ci = {};
    alloc_ci.instance         = m_instance;
    alloc_ci.physicalDevice   = m_physical_device;
    alloc_ci.device           = m_device;
    alloc_ci.vulkanApiVersion = VK_API_VERSION_1_1;

    if (vmaCreateAllocator(&alloc_ci, &m_allocator) != VK_SUCCESS)
    {
        ERRORLOG("VKDevice: failed to create VMA allocator");
        return false;
    }
    return true;
}
#endif
