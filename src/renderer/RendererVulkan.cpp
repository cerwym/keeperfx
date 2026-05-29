/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVulkan.cpp
 *     Vulkan renderer backend - implementation.
 * @par Purpose:
 *     Full IRenderer implementation.  When RENDERER_VK_SHADERC_AVAILABLE is
 *     defined (shaderc found at cmake time), the GPU render path is active:
 *     pipeline cache, descriptor pool, staging ring, VKTileAtlas, VKUIRenderer,
 *     and VKWorldViewRenderer are all initialised and driven by a dedicated
 *     render thread using the same WaitForCompletion / Signal pattern as
 *     RendererOpenGL.  Without shaderc the renderer presents black frames with
 *     software fallbacks for sub-renderers (same as the prior scaffold).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererVulkan.h"
#include "renderer/RendererManager.h"
#include "platform.h"
#include "bflib_video.h"    // MyScreenWidth / MyScreenHeight, lbDisplay
#include "bflib_render.h"   // render_fade_tables
#include "engine_lenses.h"  // lens_mode
#include "vidfade.h"        // g_palette_possession_tint, g_screen_tint
#include "globals.h"
#include <vulkan/vulkan.h>
#include <cstring>
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

    // Phase 1: create VkInstance (exposes GetInstance() for surface creation).
    if (!m_device.InitInstance())
    {
        ERRORLOG("RendererVulkan::Init: instance creation failed");
        return false;
    }

    // Phase 2: create VkSurfaceKHR via SDL.
    if (!platform_vk_create_surface(m_device.GetInstance(), &m_surface))
    {
        ERRORLOG("RendererVulkan::Init: SDL surface creation failed");
        m_device.Shutdown();
        return false;
    }

    // Phase 3: select physical device, create logical device + swapchain.
    // VKDevice::InitDevice() takes ownership of m_surface.
    if (!m_device.InitDevice(m_surface, m_width, m_height))
    {
        ERRORLOG("RendererVulkan::Init: device/swapchain creation failed");
        m_device.Shutdown();
        m_surface = VK_NULL_HANDLE;
        return false;
    }
    m_surface = VK_NULL_HANDLE; // now owned by VKDevice

    // Dummy CPU framebuffer (software rasteriser writes here; ignored on GPU path).
    m_dummy_pitch = m_width;
    m_dummy_fb.assign((size_t)m_width * (size_t)m_height, 0);

#ifdef RENDERER_VK_SHADERC_AVAILABLE
    // ---- GPU path init -------------------------------------------------------

    m_compiler = shaderc_compiler_initialize();
    if (!m_compiler)
    {
        ERRORLOG("RendererVulkan::Init: shaderc_compiler_initialize() failed");
        m_device.Shutdown();
        return false;
    }

    if (!m_pipelines.Init(m_device.GetDevice(),
                          m_device.GetRenderPass(),
                          m_compiler))
    {
        ERRORLOG("RendererVulkan::Init: VKPipelineCache::Init failed");
        goto gpu_init_fail;
    }

    if (!m_descriptors.Init(m_device.GetDevice(),
                            m_pipelines.GetDescriptorSetLayout(),
                            /*max_sets=*/512,
                            /*frames_in_flight=*/kVKFramesInFlight))
    {
        ERRORLOG("RendererVulkan::Init: VKDescriptorLayout::Init failed");
        goto gpu_init_fail;
    }

    if (!m_staging.Init(m_device.GetDevice(),
                        m_device.GetAllocator(),
                        /*total_bytes=*/32u * 1024u * 1024u))
    {
        ERRORLOG("RendererVulkan::Init: VKStagingRing::Init failed");
        goto gpu_init_fail;
    }

    if (!init_shared_textures())
    {
        ERRORLOG("RendererVulkan::Init: init_shared_textures() failed");
        goto gpu_init_fail;
    }

    // Tile atlas (owned here; passed as pointer to VKWorldViewRenderer).
    m_tile_atlas = new VKTileAtlas();
    m_tile_atlas->SetDevice(m_device.GetDevice(), m_device.GetAllocator());
    if (!m_tile_atlas->Init())
    {
        ERRORLOG("RendererVulkan::Init: VKTileAtlas::Init failed");
        goto gpu_init_fail;
    }

    // VKUIRenderer
    m_ui_renderer = new VKUIRenderer();
    if (!m_ui_renderer->Init(m_device.GetDevice(),
                             m_device.GetAllocator(),
                             &m_pipelines,
                             &m_descriptors,
                             m_pipelines.GetDescriptorSetLayout()))
    {
        ERRORLOG("RendererVulkan::Init: VKUIRenderer::Init failed");
        goto gpu_init_fail;
    }

    // VKWorldViewRenderer
    m_world_renderer = new VKWorldViewRenderer();
    if (!m_world_renderer->Init(m_device.GetDevice(),
                                m_device.GetAllocator(),
                                &m_pipelines,
                                &m_descriptors,
                                &m_staging,
                                m_tile_atlas,
                                m_pal_view,
                                m_pal_sampler,
                                m_fade_view,
                                m_fade_sampler))
    {
        ERRORLOG("RendererVulkan::Init: VKWorldViewRenderer::Init failed");
        goto gpu_init_fail;
    }

    m_ui_renderer->SetPaletteSource(lbPalette);
    m_world_renderer->SetPaletteSource(lbPalette);

    SYNCLOG("RendererVulkan: GPU path initialised (%dx%d, shaderc)", m_width, m_height);
    return true;

gpu_init_fail:
    if (m_world_renderer) { delete m_world_renderer; m_world_renderer = nullptr; }
    if (m_ui_renderer)    { delete m_ui_renderer;    m_ui_renderer    = nullptr; }
    if (m_tile_atlas)     { delete m_tile_atlas;     m_tile_atlas     = nullptr; }
    shutdown_shared_textures();
    m_staging.Shutdown();
    m_descriptors.Shutdown();
    m_pipelines.Shutdown();
    shaderc_compiler_release(m_compiler);
    m_compiler = nullptr;
    m_device.Shutdown();
    return false;

#else  // !RENDERER_VK_SHADERC_AVAILABLE
    SYNCLOG("RendererVulkan: shaderc not available - scaffold mode (%dx%d)", m_width, m_height);
    return true;
#endif
}

void RendererVulkan::Shutdown()
{
    if (!m_device.IsInitialised())
        return;

#ifdef RENDERER_VK_SHADERC_AVAILABLE
    if (m_render_thread.IsActive())
    {
        m_render_thread.WaitForCompletion();
        m_render_thread.Stop();
    }

    vkDeviceWaitIdle(m_device.GetDevice());

    // Sub-renderers are owned by RendererManager (returned by CreateVKXxx()).
    // We call Shutdown() to release GPU resources while the device is valid and
    // the GPU is idle, but do NOT delete — RendererManager will delete them.
    // If the object was never transferred (Init() partial fail), the gpu_init_fail
    // path deletes them directly.
    if (m_world_renderer) { m_world_renderer->Shutdown(); m_world_renderer = nullptr; }
    if (m_ui_renderer)    { m_ui_renderer->Shutdown();    m_ui_renderer    = nullptr; }
    if (m_tile_atlas)     { m_tile_atlas->Free();         delete m_tile_atlas;     m_tile_atlas     = nullptr; }
    shutdown_shared_textures();
    m_staging.Shutdown();
    m_descriptors.Shutdown();
    m_pipelines.Shutdown();
    if (m_compiler)
    {
        shaderc_compiler_release(m_compiler);
        m_compiler = nullptr;
    }
    m_frame_index    = 0;
    m_pal_img_ready  = false;
    m_fade_img_ready = false;
    m_fade_uploaded  = false;
#endif

    m_device.Shutdown();
    m_dummy_fb.clear();
    m_frame_begun  = false;
    m_needs_resize = false;
    SYNCLOG("RendererVulkan: shutdown");
}

/******************************************************************************/
// Per-frame
/******************************************************************************/

bool RendererVulkan::BeginFrame()
{
    if (m_frame_begun) return true;

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

    m_frame_begun = true;

#ifdef RENDERER_VK_SHADERC_AVAILABLE
    m_render_graph.BeginFrame();

    if (MyScreenWidth > 0 && MyScreenHeight > 0)
    {
        m_width  = (int)MyScreenWidth;
        m_height = (int)MyScreenHeight;
    }

    if (!RendererIsFadeCachePreserved())
    {
        if (m_ui_renderer) m_ui_renderer->SetUICommandBuffers(&m_render_graph.GetUIBuffers());
        // VKWorldViewRenderer manages its own internal double-buffer;
        // game-thread calls to BeginWorldPass/DrawIsometricView write there directly.
    }

    // Text write window is always opened.
    {
        auto* tr = RendererGetTextRenderer();
        if (tr) tr->SetTextCommandBuffers(&m_render_graph.GetTextBuffers());
    }
    // Cursor write window.
    {
        auto* cl = RendererGetCursorLayer();
        if (cl) cl->SetCursorWriteBuffers(&m_render_graph.GetUIBuffers());
    }
#endif

    return true;
}

void RendererVulkan::EndFrame()
{
    if (!m_frame_begun)
        return;

#ifdef RENDERER_VK_SHADERC_AVAILABLE
    if (m_ui_renderer || m_world_renderer)
    {
        // Wait for the PREVIOUS frame's render thread to finish before flipping.
        m_render_thread.WaitForCompletion();

        // Close IR write windows.
        if (m_ui_renderer) m_ui_renderer->SetUICommandBuffers(nullptr);
        // VKWorldViewRenderer: no SetWorldCommandBuffers - uses its own internal state.
        {
            auto* tr = RendererGetTextRenderer();
            if (tr) tr->SetTextCommandBuffers(nullptr);
            auto* cl = RendererGetCursorLayer();
            if (cl) cl->SetCursorWriteBuffers(nullptr);
        }

        // Snapshot per-frame state for the render thread.
        std::memcpy(m_rt_frame_state.palette, lbPalette, sizeof(m_rt_frame_state.palette));
        m_rt_frame_state.possession_tint = g_palette_possession_tint;
        std::memcpy(m_rt_frame_state.screen_tint, g_screen_tint, sizeof(m_rt_frame_state.screen_tint));
        m_rt_frame_state.lens_mode = lens_mode;
        m_rt_frame_state.screen_w  = m_width;
        m_rt_frame_state.screen_h  = m_height;

        // Capture the swapchain image index for the render thread.
        // Must be set before Signal() to avoid a race where the next frame's
        // BeginFrame() overwrites m_current_image_index.
        m_rt_image_index = m_current_image_index;

        // Flush map-fade IR command before flip.
        if (auto* mfp = RendererGetMapFadePass())
            mfp->FlushToRenderGraph(m_render_graph);

        if (RendererIsFadeCachePreserved())
        {
            m_render_graph.UpdateFrameState(m_rt_frame_state);
        }
        else
        {
            if (m_world_renderer) m_world_renderer->FlipBuffers();
            if (m_ui_renderer)    m_ui_renderer->FlipBuffers();
            m_render_graph.Flip(m_rt_frame_state);
        }

        // Lazily start render thread on first EndFrame().
        if (!m_render_thread.IsActive())
        {
            m_render_thread.Start(
                [this]() { /* no per-thread context needed for Vulkan */ },
                [this]() { EndFrame_VK(); },
                [this]() { /* nothing to release */ }
            );
        }

        m_render_thread.Signal();
        m_frame_begun = false;
        return;
    }
#endif

    // Scaffold: present a plain black-cleared frame.
    m_frame_begun = false;

    VkCommandBuffer cmd;
    if (!m_device.BeginFrame(cmd))
        return;

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
    vkCmdEndRenderPass(cmd);

    VkResult result = m_device.SubmitAndPresent(cmd, m_current_image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_needs_resize = true;
}

#ifdef RENDERER_VK_SHADERC_AVAILABLE

/** Transition a VkImage into the appropriate layout before a copy. */
static void transition_image(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout old_layout, VkImageLayout new_layout,
                             VkAccessFlags src_access, VkAccessFlags dst_access,
                             VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier bar = {};
    bar.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout           = old_layout;
    bar.newLayout           = new_layout;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image               = image;
    bar.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bar.srcAccessMask       = src_access;
    bar.dstAccessMask       = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &bar);
}

/** Render thread work function: record and submit one complete VK frame. */
void RendererVulkan::EndFrame_VK()
{
    const int      fi        = m_frame_index;
    const uint32_t image_idx = m_rt_image_index; // safe local copy

    VkCommandBuffer cmd;
    if (!m_device.BeginFrame(cmd))
        return;

    m_staging.BeginFrame(fi);
    m_descriptors.BeginFrame(fi);

    // ---- Pre-pass: upload changed palette texture ---------------------------
    {
        bool palette_changed = (std::memcmp(m_last_palette, m_rt_frame_state.palette, 768) != 0);
        if (palette_changed || !m_pal_img_ready)
        {
            std::memcpy(m_last_palette, m_rt_frame_state.palette, 768);

            uint8_t rgba[256 * 4];
            for (int i = 0; i < 256; ++i)
            {
                rgba[i * 4 + 0] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 0] << 2);
                rgba[i * 4 + 1] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 1] << 2);
                rgba[i * 4 + 2] = (uint8_t)(m_rt_frame_state.palette[i * 3 + 2] << 2);
                rgba[i * 4 + 3] = 255;
            }

            VKStagingAlloc sa;
            if (m_staging.Alloc(rgba, sizeof(rgba), 4, sa))
            {
                transition_image(cmd, m_pal_image,
                    m_pal_img_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    m_pal_img_ready ? VK_ACCESS_SHADER_READ_BIT : 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    m_pal_img_ready ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT);

                VkBufferImageCopy copy = {};
                copy.bufferOffset      = sa.offset;
                copy.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent       = {256, 1, 1};
                vkCmdCopyBufferToImage(cmd, sa.buffer, m_pal_image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

                transition_image(cmd, m_pal_image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

                m_pal_img_ready = true;
            }
        }
    }

    // ---- Pre-pass: upload fade table (once, when game tables are ready) -----
    // render_fade_tables is a flat 256×256 byte array (same layout used by GL).
    if (!m_fade_uploaded && render_fade_tables != nullptr)
    {
        VKStagingAlloc sa;
        if (m_staging.Alloc(render_fade_tables, 256u * 256u, 4, sa))
        {
            transition_image(cmd, m_fade_image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkBufferImageCopy copy = {};
            copy.bufferOffset      = sa.offset;
            copy.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent       = {256, 256, 1};
            vkCmdCopyBufferToImage(cmd, sa.buffer, m_fade_image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            transition_image(cmd, m_fade_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            m_fade_img_ready = true;
            m_fade_uploaded  = true;
        }
    }

    // ---- Flush sub-renderer pending atlas uploads (before render pass) ------
    if (m_ui_renderer)    m_ui_renderer->FlushPendingInit(cmd, m_staging);
    if (m_world_renderer) m_world_renderer->FlushPendingInit(cmd);

    // ---- Push snapshotted screen dimensions to sub-renderers ----------------
    {
        const int sw = m_rt_frame_state.screen_w;
        const int sh = m_rt_frame_state.screen_h;
        if (m_ui_renderer)    m_ui_renderer->SetScreenSize(sw, sh);
        if (m_world_renderer) m_world_renderer->SetScreenSize(sw, sh);
    }

    // ---- Main render pass ---------------------------------------------------
    VkClearValue clear_val = {};
    clear_val.color.float32[3] = 1.0f;

    VkExtent2D ext = m_device.GetExtent();

    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass        = m_device.GetRenderPass();
    rp_begin.framebuffer       = m_device.GetFramebuffer(image_idx);
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = ext;
    rp_begin.clearValueCount   = 1;
    rp_begin.pClearValues      = &clear_val;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Supply the command buffer + frame index to sub-renderers.
    if (m_ui_renderer)    m_ui_renderer->SetCommandBuffer(cmd, fi);
    if (m_world_renderer) m_world_renderer->SetCommandBuffer(cmd, fi);

    // RenderGraph::Execute() calls PopulateFromIR() on each sub-renderer,
    // then drives Draw* callbacks in the correct layer order.
    m_render_graph.Execute(GetCapabilities(),
                           m_world_renderer,
                           m_ui_renderer,
                           RendererGetTextRenderer(),
                           nullptr,  // IShadowRenderer - not yet implemented
                           nullptr); // IDebugRenderer  - not yet implemented

    vkCmdEndRenderPass(cmd);

    VkResult result = m_device.SubmitAndPresent(cmd, image_idx);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        m_needs_resize = true;

    m_frame_index = (m_frame_index + 1) % kVKFramesInFlight;
}

bool RendererVulkan::init_shared_textures()
{
    VkDevice      device    = m_device.GetDevice();
    VmaAllocator  allocator = m_device.GetAllocator();

    // ---- Palette texture: 256x1 VK_FORMAT_R8G8B8A8_UNORM -------------------
    {
        VkImageCreateInfo ci = {};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_1D;
        ci.format        = VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent        = {256, 1, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai = {};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &ci, &ai, &m_pal_image, &m_pal_alloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo vi = {};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = m_pal_image;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_1D;
        vi.format           = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &m_pal_view) != VK_SUCCESS)
            return false;

        VkSamplerCreateInfo si = {};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &si, nullptr, &m_pal_sampler) != VK_SUCCESS)
            return false;
    }

    // ---- Fade table: 256x256 VK_FORMAT_R8_UNORM ----------------------------
    {
        VkImageCreateInfo ci = {};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = VK_FORMAT_R8_UNORM;
        ci.extent        = {256, 256, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo ai = {};
        ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &ci, &ai, &m_fade_image, &m_fade_alloc, nullptr) != VK_SUCCESS)
            return false;

        VkImageViewCreateInfo vi = {};
        vi.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image            = m_fade_image;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = VK_FORMAT_R8_UNORM;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device, &vi, nullptr, &m_fade_view) != VK_SUCCESS)
            return false;

        VkSamplerCreateInfo si = {};
        si.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter    = VK_FILTER_NEAREST;
        si.minFilter    = VK_FILTER_NEAREST;
        si.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &si, nullptr, &m_fade_sampler) != VK_SUCCESS)
            return false;
    }

    return true;
}

void RendererVulkan::shutdown_shared_textures()
{
    VkDevice     device    = m_device.GetDevice();
    VmaAllocator allocator = m_device.GetAllocator();

    if (m_fade_sampler) { vkDestroySampler(device, m_fade_sampler, nullptr);   m_fade_sampler = VK_NULL_HANDLE; }
    if (m_fade_view)    { vkDestroyImageView(device, m_fade_view, nullptr);    m_fade_view    = VK_NULL_HANDLE; }
    if (m_fade_image)   { vmaDestroyImage(allocator, m_fade_image, m_fade_alloc); m_fade_image = VK_NULL_HANDLE; }
    if (m_pal_sampler)  { vkDestroySampler(device, m_pal_sampler, nullptr);    m_pal_sampler  = VK_NULL_HANDLE; }
    if (m_pal_view)     { vkDestroyImageView(device, m_pal_view, nullptr);     m_pal_view     = VK_NULL_HANDLE; }
    if (m_pal_image)    { vmaDestroyImage(allocator, m_pal_image, m_pal_alloc); m_pal_image   = VK_NULL_HANDLE; }
}

#endif // RENDERER_VK_SHADERC_AVAILABLE

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
    // No-op: dummy buffer is ignored on the GPU path.
}

void RendererVulkan::FlushRenderWork()
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    if (m_render_thread.IsActive())
    {
        m_render_thread.WaitForCompletion();
        return;
    }
#endif
    if (m_device.IsInitialised())
        vkDeviceWaitIdle(m_device.GetDevice());
}

/******************************************************************************/
// Metadata
/******************************************************************************/

const char* RendererVulkan::GetName() const
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    return "Vulkan";
#else
    return "Vulkan (scaffold)";
#endif
}

bool RendererVulkan::SupportsRuntimeSwitch() const
{
    return true;
}

BackendCapabilities RendererVulkan::GetCapabilities() const
{
    BackendCapabilities caps = {};
    caps.supportsRuntimeSwitch = 1;
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    if (m_ui_renderer && m_world_renderer)
    {
        caps.hasGPURenderPath        = 1;
        caps.wantsFullscreenViewport = 1;
        caps.hasGPUOverheadMap       = 1;
        caps.hasGPUMapFade           = 0;  // VKMapFadePass is SW fallback for now
        caps.hasGPUOverlay           = 1;
        caps.hasGPULandviewZoom      = 0;  // not yet implemented
        caps.hasGPUVideoFrame        = 0;  // not yet implemented
        caps.hasGPUSprites           = 1;
        caps.hasSwipeOverlay         = 1;
        caps.supportsGPUPasses       = 1;
    }
#endif
    return caps;
}

/******************************************************************************/
// Sub-renderer access
/******************************************************************************/

IWorldViewRenderer* RendererVulkan::GetWorldViewRenderer()
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    return m_world_renderer;
#else
    return nullptr;
#endif
}

IUIRenderer* RendererVulkan::GetUIRenderer()
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    return m_ui_renderer;
#else
    return nullptr;
#endif
}

/******************************************************************************/
// Factory methods (called by RendererManager create_* factories)
/******************************************************************************/

IWorldViewRenderer* RendererVulkan::CreateVKWorldViewRenderer()
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    return m_world_renderer;
#else
    return nullptr;
#endif
}

IUIRenderer* RendererVulkan::CreateVKUIRenderer()
{
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    return m_ui_renderer;
#else
    return nullptr;
#endif
}
