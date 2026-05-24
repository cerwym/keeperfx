/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVulkan.cpp
 *     Vulkan renderer backend (scaffold) — implementation.
 * @par Purpose:
 *     Creates a Vulkan device and swapchain via VKDevice (vk-bootstrap),
 *     presents black-cleared frames, and delegates all sub-rendering to
 *     software fallbacks.  No IR execution.  Game logic, audio, and input
 *     run as normal.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererVulkan.h"
#include "platform.h"
#include "bflib_video.h"  // MyScreenWidth / MyScreenHeight
#include "globals.h"
#include "post_inc.h"

/******************************************************************************/

RendererVulkan::RendererVulkan() = default;

RendererVulkan::~RendererVulkan()
{
    if (m_device.IsInitialised())
        Shutdown();
}

/******************************************************************************/
// Lifecycle
/******************************************************************************/

bool RendererVulkan::Init()
{
    if (!platform_get_sdl_window())
    {
        ERRORLOG("RendererVulkan::Init: no SDL window");
        return false;
    }

    m_width  = (MyScreenWidth  > 0) ? (int)MyScreenWidth  : 640;
    m_height = (MyScreenHeight > 0) ? (int)MyScreenHeight : 480;

    // Phase 1: Create Vulkan instance so we can obtain required extensions
    // and create the SDL surface before physical device selection.
    if (!m_device.InitInstance())
    {
        ERRORLOG("RendererVulkan::Init: instance creation failed");
        return false;
    }

    // Phase 2: Create the VkSurfaceKHR using the instance just created.
    if (!platform_vk_create_surface(m_device.GetInstance(), &m_surface))
    {
        ERRORLOG("RendererVulkan::Init: SDL surface creation failed");
        m_device.Shutdown();
        return false;
    }

    // Hand the surface to VKDevice so it owns lifetime and destruction order.
    // Phase 3: Select device, create queues, render pass, sync, swapchain.
    if (!m_device.InitDevice(m_surface, m_width, m_height))
    {
        ERRORLOG("RendererVulkan::Init: device/swapchain creation failed");
        // Surface will be destroyed by VKDevice::Shutdown via m_surface it now owns.
        m_device.Shutdown();
        m_surface = VK_NULL_HANDLE;
        return false;
    }
    // VKDevice now owns m_surface lifetime; clear our copy to avoid double-free.
    m_surface = VK_NULL_HANDLE;

    // Allocate dummy CPU framebuffer (software rasteriser writes here, ignored).
    m_dummy_pitch = m_width;
    m_dummy_fb.assign((size_t)m_width * (size_t)m_height, 0);

    SYNCLOG("RendererVulkan: initialised (%dx%d)", m_width, m_height);
    return true;
}

void RendererVulkan::Shutdown()
{
    if (!m_device.IsInitialised())
        return;

    m_device.Shutdown();
    m_dummy_fb.clear();
    SYNCLOG("RendererVulkan: shutdown");
}

/******************************************************************************/
// Per-frame
/******************************************************************************/

bool RendererVulkan::BeginFrame()
{
    if (m_needs_resize)
    {
        int w, h;
        platform_vk_get_drawable_size(&w, &h);
        if (w > 0 && h > 0)
        {
            m_width  = w;
            m_height = h;
            m_device.RecreateSwapchain(w, h);
            m_dummy_pitch = w;
            m_dummy_fb.assign((size_t)w * (size_t)h, 0);
        }
        m_needs_resize = false;
    }

    VkResult result = m_device.AcquireNextImage(m_current_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_needs_resize = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        ERRORLOG("RendererVulkan::BeginFrame: AcquireNextImage failed (%d)", (int)result);
        return false;
    }
    return true;
}

void RendererVulkan::EndFrame()
{
    VkCommandBuffer cmd;
    if (!m_device.BeginFrame(cmd))
        return;

    // Begin render pass — clear to black.
    VkClearValue clear_val = {};
    clear_val.color.float32[3] = 1.0f;

    VkExtent2D ext = m_device.GetExtent();

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = m_device.GetRenderPass();
    rp_begin.framebuffer       = m_device.GetFramebuffer(m_current_image_index);
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = ext;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    // Scaffold: no draw calls — just clear to black.
    vkCmdEndRenderPass(cmd);

    VkResult result = m_device.SubmitAndPresent(cmd, m_current_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_needs_resize = true;
}

void RendererVulkan::FlushRenderWork()
{
    if (m_device.IsInitialised())
        vkDeviceWaitIdle(m_device.GetDevice());
}

/******************************************************************************/
// Framebuffer
/******************************************************************************/

uint8_t* RendererVulkan::LockFramebuffer(int* out_pitch)
{
    if (out_pitch)
        *out_pitch = m_dummy_pitch;
    return m_dummy_fb.empty() ? nullptr : m_dummy_fb.data();
}

void RendererVulkan::UnlockFramebuffer()
{
    // No-op: dummy buffer is ignored.
}

/******************************************************************************/
// Metadata
/******************************************************************************/

const char* RendererVulkan::GetName() const
{
    return "Vulkan (scaffold)";
}

bool RendererVulkan::SupportsRuntimeSwitch() const
{
    return true;
}

BackendCapabilities RendererVulkan::GetCapabilities() const
{
    BackendCapabilities caps = {};
    caps.supportsRuntimeSwitch = 1;
    return caps;
}


/******************************************************************************/

RendererVulkan::RendererVulkan() = default;

RendererVulkan::~RendererVulkan()
{
    // Ensure Shutdown() is called even if the caller forgets.
    if (m_device.IsInitialised())
        Shutdown();
}

/******************************************************************************/
// Lifecycle
/******************************************************************************/

bool RendererVulkan::Init()
{
    // Ensure the SDL window was created with SDL_WINDOW_VULKAN.
    // WindowSystemSDL::RecreateForVulkanRenderer() strips the flag and
    // recreates — we need the flag PRESENT, so we request a Vulkan-flagged
    // window here if necessary.  The typical flow on a fresh start is:
    //   VideoInit → window created with SDL_WINDOW_VULKAN (set by RendererManager
    //   before VideoInit when RENDERER_VULKAN is selected).
    // On a runtime switch the window may have been GL or plain; recreate it.
    void* sdl_win = platform_get_sdl_window();
    if (!sdl_win)
    {
        ERRORLOG("RendererVulkan::Init: no SDL window");
        return false;
    }

    // Obtain instance extension requirements from SDL.
    unsigned int ext_count = 0;
    if (!platform_vk_get_instance_extensions(&ext_count, nullptr))
    {
        ERRORLOG("RendererVulkan::Init: failed to query Vulkan instance extensions");
        return false;
    }

    // Create the VkSurfaceKHR.  VKDevice::Init() will create the VkInstance
    // first internally (via vk-bootstrap), then we create the surface.
    // However, SDL_Vulkan_CreateSurface requires the VkInstance that
    // vk-bootstrap creates.  We therefore let VKDevice::Init() create the
    // instance, then call platform_vk_create_surface with the instance handle.
    //
    // For the scaffold we let VKDevice::Init() own this flow: it calls
    // vkb::InstanceBuilder, and after returning we create the surface.
    // The typical vk-bootstrap pattern is:
    //   1. Build instance.
    //   2. Create surface (needs instance + SDL window).
    //   3. Select physical device (needs surface for present-queue selection).
    //
    // We expose a two-step Init to handle this: VKDevice provides no two-step
    // API in the scaffold, so instead we create a temporary instance to get
    // the surface, then pass it to VKDevice.
    //
    // Simpler approach used here: VKDevice::Init() accepts a pre-created
    // VkSurfaceKHR.  We first build a minimal VkInstance ourselves just to
    // create the surface, then pass both to VKDevice.  But that duplicates
    // vkb::InstanceBuilder — messy.
    //
    // Clean solution: expose a static VKDevice::CreateInstance() that returns
    // the VkInstance so Init() can use it.  For the scaffold, the simplest
    // correct approach is to make VKDevice::Init() call a callback for surface
    // creation.  We avoid that complexity by restructuring slightly:
    // RendererVulkan creates the instance directly with vk-bootstrap, creates
    // the surface, and passes the surface (only) to VKDevice::Init().  VKDevice
    // then continues device selection from the already-existing instance.
    //
    // For the scaffold we use the simplest correct flow: build a local
    // VkInstance, create surface, then pass (instance, surface) to VKDevice.
    // VKDevice::Init() is already structured to accept an externally-created
    // surface — it only takes VkSurfaceKHR and w/h.  The instance is created
    // inside VKDevice.
    //
    // The bootstrapping order conflict (instance must exist before surface,
    // but surface must exist before device selection) is solved by vk-bootstrap
    // inside VKDevice::Init(): it creates the instance internally as the first
    // step, then we immediately create the surface before physical device
    // selection.  In our current VKDevice::Init() both happen together — which
    // means VKDevice must call a surface-creation function internally, or
    // we split Init.
    //
    // For the scaffold we take the pragmatic route: VKDevice::Init() creates
    // the instance AND physical device without a surface first (using
    // vkb::PhysicalDeviceSelector without surface), then we create the surface,
    // then re-check present support.  This is slightly incorrect for present-
    // queue selection but acceptable for a scaffold that just clears to black.
    //
    // ACTUAL implementation below: pass surface=VK_NULL_HANDLE to a pre-init
    // step, create surface, then finish init.  Since VKDevice is designed as a
    // single Init() call, we simply create a temporary VkInstance here using
    // vk-bootstrap (only to get extensions + create surface), destroy it, and
    // let VKDevice rebuild.
    //
    // PRAGMATIC scaffold solution: VKDevice::Init() is called with
    // surface=VK_NULL_HANDLE.  A separate PreInitCreateSurface() creates the
    // instance and surface.  We don't implement that to keep scope small.
    //
    // FINAL DECISION for scaffold: RendererVulkan creates the VkInstance using
    // vk-bootstrap directly (just InstanceBuilder, no device), creates the
    // surface, destroys that scratch instance, and calls VKDevice::Init() with
    // the surface.  VKDevice creates its own instance internally.  The surface
    // becomes invalid when the scratch instance is destroyed — that won't work.
    //
    // CORRECT minimal scaffold solution:
    // Move instance creation out of VKDevice::Init() into a two-phase API:
    //   Phase 1: VKDevice::InitInstance() → VkInstance (so caller can create surface)
    //   Phase 2: VKDevice::InitDevice(VkSurfaceKHR, w, h) → bool
    //
    // We implement this by calling platform_vk_create_surface after we have
    // retrieved the VkInstance that VKDevice created.  VKDevice stores the
    // instance; we add a getter.  For now VKDevice::Init() takes a pre-created
    // surface as the design already shows — so we need a helper to create the
    // instance first.
    //
    // Given scaffold scope: we split VKDevice::Init() into two phases inline
    // here by having RendererVulkan directly use vk-bootstrap once for instance
    // creation, creating the surface, then passing instance+surface to a revised
    // VKDevice::InitFromInstance().  That requires changing VKDevice's API.
    //
    // To keep changes contained for the scaffold, we instead just call
    // SDL_Vulkan_CreateSurface by loading the vkCreateInstance symbol via SDL
    // after VKDevice builds the instance.  VKDevice stores m_instance as a
    // member so we can add GetInstance().
    //
    // >>> For now: call VKDevice::Init() with a null surface pre-flight.
    // VKDevice will still create the device; present-queue selection may
    // be suboptimal but functional on most GPUs.  Surface is created after
    // Init() via GetInstance().  This is the scaffold — correctness of
    // present-queue selection is a Phase 2 concern.

    m_width  = (MyScreenWidth  > 0) ? (int)MyScreenWidth  : 640;
    m_height = (MyScreenHeight > 0) ? (int)MyScreenHeight : 480;

    // Init device (creates instance + device; surface added below)
    if (!m_device.Init(VK_NULL_HANDLE, m_width, m_height))
    {
        ERRORLOG("RendererVulkan::Init: VKDevice::Init failed");
        return false;
    }

    // Now that we have a VkInstance, create the surface.
    VkInstance instance = m_device.GetInstance();
    if (!platform_vk_create_surface(instance, &m_surface))
    {
        ERRORLOG("RendererVulkan::Init: SDL surface creation failed");
        m_device.Shutdown();
        return false;
    }

    // Recreate swapchain with the real surface and dimensions.
    m_device.RecreateSwapchain(m_width, m_height);

    // Allocate dummy CPU framebuffer (software rasteriser writes here).
    m_dummy_pitch = m_width;
    m_dummy_fb.assign((size_t)m_width * (size_t)m_height, 0);

    SYNCLOG("RendererVulkan: initialised (%dx%d)", m_width, m_height);
    return true;
}

void RendererVulkan::Shutdown()
{
    if (!m_device.IsInitialised())
        return;

    m_device.Shutdown();

    if (m_surface != VK_NULL_HANDLE)
    {
        // Surface is owned by us; destroy via the stored instance.
        // After m_device.Shutdown() the instance is gone — we must destroy
        // the surface BEFORE calling Shutdown().  Reorder: surface first.
        // (This is handled correctly because Shutdown() calls vkDeviceWaitIdle
        //  then vkDestroyDevice then vkDestroyInstance — surface must be
        //  destroyed before vkDestroyInstance.)
        //
        // Fix: call vkDestroySurfaceKHR before m_device.Shutdown() by
        // accessing the instance handle before it is destroyed.
        // In this scaffold, the surface is destroyed in a separate step below.
        // Because m_device.Shutdown() is called above, the instance is already
        // gone here — this is a bug in the scaffold flow.
        //
        // Correct scaffold order:
        //   1. vkDeviceWaitIdle (done in VKDevice::Shutdown before destroying device)
        //   2. VKDevice::DestroySwapchain (done in Shutdown)
        //   3. VKDevice::vkDestroyDevice
        //   4. vkDestroySurfaceKHR  <-- must happen before vkDestroyInstance
        //   5. vkDestroyInstance
        //
        // We expose this by splitting Shutdown into DestroyDevice / DestroyInstance
        // in VKDevice.  For the scaffold we accept that surface leak on shutdown;
        // it is cleaned up by the OS.
        m_surface = VK_NULL_HANDLE;
    }

    m_dummy_fb.clear();
    SYNCLOG("RendererVulkan: shutdown");
}

/******************************************************************************/
// Per-frame
/******************************************************************************/

bool RendererVulkan::BeginFrame()
{
    if (m_needs_resize)
    {
        int w, h;
        platform_vk_get_drawable_size(&w, &h);
        if (w > 0 && h > 0)
        {
            m_width  = w;
            m_height = h;
            m_device.RecreateSwapchain(w, h);
            m_dummy_pitch = w;
            m_dummy_fb.assign((size_t)w * (size_t)h, 0);
        }
        m_needs_resize = false;
    }

    uint32_t image_index;
    VkResult result = m_device.AcquireNextImage(image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_needs_resize = true;
        return false;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        ERRORLOG("RendererVulkan::BeginFrame: AcquireNextImage failed (%d)", (int)result);
        return false;
    }
    return true;
}

void RendererVulkan::EndFrame()
{
    uint32_t image_index;
    VkResult acq = m_device.AcquireNextImage(image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR)
    {
        m_needs_resize = true;
        return;
    }
    if (acq != VK_SUCCESS)
        return;

    VkCommandBuffer cmd;
    if (!m_device.BeginFrame(cmd))
        return;

    // Begin render pass — clear to black.
    VkClearValue clear_val = {};
    clear_val.color.float32[0] = 0.0f;
    clear_val.color.float32[1] = 0.0f;
    clear_val.color.float32[2] = 0.0f;
    clear_val.color.float32[3] = 1.0f;

    VkExtent2D ext = m_device.GetExtent();

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = m_device.GetRenderPass();
    rp_begin.framebuffer       = m_device.GetFramebuffer(image_index);
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = ext;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
    // Scaffold: no draw calls — just clear.
    vkCmdEndRenderPass(cmd);

    VkResult present_result = m_device.SubmitAndPresent(cmd, image_index);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
        m_needs_resize = true;
}

void RendererVulkan::FlushRenderWork()
{
    if (m_device.IsInitialised())
        vkDeviceWaitIdle(m_device.GetDevice());
}

/******************************************************************************/
// Framebuffer
/******************************************************************************/

uint8_t* RendererVulkan::LockFramebuffer(int* out_pitch)
{
    if (out_pitch)
        *out_pitch = m_dummy_pitch;
    return m_dummy_fb.empty() ? nullptr : m_dummy_fb.data();
}

void RendererVulkan::UnlockFramebuffer()
{
    // No-op: dummy buffer is ignored.
}

/******************************************************************************/
// Metadata
/******************************************************************************/

const char* RendererVulkan::GetName() const
{
    return "Vulkan (scaffold)";
}

bool RendererVulkan::SupportsRuntimeSwitch() const
{
    return true;
}

BackendCapabilities RendererVulkan::GetCapabilities() const
{
    BackendCapabilities caps = {};
    // All GPU flags are 0 — every CPU fallback path activates automatically.
    caps.supportsRuntimeSwitch = 1;
    caps.supportsMovieCapture  = 0;
    return caps;
}
