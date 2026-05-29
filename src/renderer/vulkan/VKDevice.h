/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKDevice.h
 *     Vulkan instance / device / swapchain RAII wrapper.
 * @par Purpose:
 *     Owns the Vulkan instance, physical/logical device, queues, swapchain,
 *     per-frame command buffers, and synchronisation primitives.  Built on
 *     vk-bootstrap to minimise boilerplate.  Used exclusively by RendererVulkan.
 */
/******************************************************************************/
#ifndef VKDEVICE_H
#define VKDEVICE_H

#include <vulkan/vulkan.h>
#include <vector>

#ifdef RENDERER_VULKAN_ENABLED
#  include <vma/vk_mem_alloc.h>
#endif

// Maximum frames-in-flight.  Two lets the CPU prepare frame N+1 while the GPU
// finishes frame N without stalling either side.
static constexpr int kVKFramesInFlight = 2;

/******************************************************************************/

class VKDevice
{
public:
    VKDevice() = default;
    VKDevice(const VKDevice&) = delete;
    VKDevice& operator=(const VKDevice&) = delete;

    /** Phase 1: Create the Vulkan instance only.
     *  Returns the VkInstance so the caller can create a VkSurfaceKHR before
     *  physical device selection (which requires the surface for present-queue
     *  verification).
     *  @return true on success; call GetInstance() to retrieve the handle. */
    bool InitInstance();

    /** Phase 2: Select physical/logical device, create swapchain and sync objects.
     *  Must be called after InitInstance() and after the caller has created a
     *  VkSurfaceKHR via platform_vk_create_surface().
     *  @param surface  Pre-created surface for this window.
     *  @param w, h     Initial drawable pixel dimensions.
     *  @return true on success. */
    bool InitDevice(VkSurfaceKHR surface, int w, int h);

    /** Destroy all Vulkan objects.  Safe to call even if Init*() was never called
     *  or returned false (no-op in that case). */
    void Shutdown();

    /** Acquire the next swapchain image.
     *  @param out_image_index  Receives the index of the acquired image.
     *  @return VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR, or error. */
    VkResult AcquireNextImage(uint32_t& out_image_index);

    /** Record the begin of a frame into the current frame's command buffer.
     *  @param out_cmd  Receives the primary VkCommandBuffer ready to record.
     *  @return true on success. */
    bool BeginFrame(VkCommandBuffer& out_cmd);

    /** Submit the recorded command buffer and present the acquired image.
     *  @param cmd          The command buffer returned by BeginFrame().
     *  @param image_index  The index returned by AcquireNextImage().
     *  @return VK_SUCCESS, VK_SUBOPTIMAL_KHR, VK_ERROR_OUT_OF_DATE_KHR, or error. */
    VkResult SubmitAndPresent(VkCommandBuffer cmd, uint32_t image_index);

    /** Destroy and recreate the swapchain, framebuffers, and image views at
     *  the new dimensions.  Does not recreate the render pass or device. */
    void RecreateSwapchain(int w, int h);

    // ---- Accessors ----

    VkInstance       GetInstance()       const { return m_instance; }
    VkDevice         GetDevice()         const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physical_device; }
    VkRenderPass     GetRenderPass()     const { return m_render_pass; }
    VkExtent2D       GetExtent()         const { return m_extent; }
    VkQueue          GetGraphicsQueue()  const { return m_graphics_queue; }
    uint32_t         GetGraphicsQueueIndex() const { return m_graphics_queue_index; }
    bool             IsInitialised()     const { return m_instance != VK_NULL_HANDLE; }
#ifdef RENDERER_VULKAN_ENABLED
    VmaAllocator     GetAllocator()      const { return m_allocator; }
#endif

    /** Return the framebuffer for the swapchain image at index. */
    VkFramebuffer    GetFramebuffer(uint32_t index) const;

private:
    bool CreateSwapchain(int w, int h);
    void DestroySwapchain();

    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateSyncObjects();
    void DestroySyncObjects();

    // --- vk-bootstrap objects (kept for swapchain recreation) ---
    VkInstance               m_instance        = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physical_device = VK_NULL_HANDLE;
    VkDevice                 m_device          = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;

    VkQueue  m_graphics_queue       = VK_NULL_HANDLE;
    uint32_t m_graphics_queue_index = 0;
    VkQueue  m_present_queue        = VK_NULL_HANDLE;
    uint32_t m_present_queue_index  = 0;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    // --- Swapchain ---
    VkSwapchainKHR           m_swapchain          = VK_NULL_HANDLE;
    VkFormat                 m_swapchain_format   = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent             = {0, 0};
    std::vector<VkImage>     m_swapchain_images;
    std::vector<VkImageView> m_swapchain_image_views;

    // --- Render pass ---
    VkRenderPass m_render_pass = VK_NULL_HANDLE;

    // --- Framebuffers (one per swapchain image) ---
    std::vector<VkFramebuffer> m_framebuffers;

    // --- Command pool and per-frame command buffers ---
    VkCommandPool                   m_command_pool = VK_NULL_HANDLE;
    VkCommandBuffer                 m_cmd_bufs[kVKFramesInFlight] = {};

    // --- Per-frame sync primitives ---
    VkSemaphore m_image_available[kVKFramesInFlight]  = {};
    VkSemaphore m_render_finished[kVKFramesInFlight]  = {};
    VkFence     m_in_flight_fences[kVKFramesInFlight] = {};

    int      m_current_frame = 0;

#ifdef RENDERER_VULKAN_ENABLED
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    bool CreateAllocator();
#endif
};

#endif // VKDEVICE_H
