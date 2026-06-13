/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVulkan.h
 *     Vulkan renderer backend.
 * @par Purpose:
 *     Full IRenderer implementation using Vulkan.  Owns VKDevice, the staging
 *     ring, pipeline cache, descriptor pool, tile atlas, UI renderer, and world
 *     renderer.  Runs a dedicated render thread (same WaitForCompletion/Signal
 *     pattern as RendererOpenGL) so game logic is never stalled by GPU work.
 */
/******************************************************************************/
#pragma once
#ifndef RENDERERVULKAN_H
#define RENDERERVULKAN_H

#include "renderer/IRenderer.h"
#include "renderer/FrameState.h"
#include "renderer/RenderGraph.h"
#include "renderer/RenderThreadManager.h"
#include "renderer/vulkan/VKDevice.h"
#include "renderer/vulkan/VKMapFadePass.h"
#include "renderer/vulkan/VKTextRenderer.h"

#ifdef RENDERER_VK_SHADERC_AVAILABLE
#  include "renderer/vulkan/VKStagingRing.h"
#  include "renderer/vulkan/VKPipelineCache.h"
#  include "renderer/vulkan/VKDescriptorLayout.h"
#  include "renderer/vulkan/VKTileAtlas.h"
#  include "renderer/vulkan/VKUIRenderer.h"
#  include "renderer/vulkan/VKWorldViewRenderer.h"
#  include <shaderc/shaderc.h>
#endif

#include <vulkan/vulkan.h>
#include <atomic>

/******************************************************************************/

class RendererVulkan : public IRenderer
{
public:
    RendererVulkan();
    ~RendererVulkan() override;

    // ---- Lifecycle ----
    bool Init() override;
    void Shutdown() override;

    // ---- Per-frame ----
    bool BeginFrame() override;
    void EndFrame() override;
    void FlushRenderWork() override;
    void ClearScreen(uint8_t colour_index) override { (void)colour_index; }

    // ---- Framebuffer ----
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;

    // ---- Metadata ----
    const char*         GetName()              const override;
    bool                SupportsRuntimeSwitch() const override;
    BackendCapabilities GetCapabilities()       const override;

    // ---- Sub-renderer access ----
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass*       GetMapFadePass()        override { return &m_mapfade; }
    ITextRenderer*      GetTextRenderer()       override { return &m_text_renderer; }
    IUIRenderer*        GetUIRenderer()         override;

    // ---- Factory methods (called by RendererManager) ----
    IWorldViewRenderer* CreateVKWorldViewRenderer();
    IUIRenderer*        CreateVKUIRenderer();

    // ---- Sub-renderer setters (called by RendererManager factories) ----
#ifdef RENDERER_VK_SHADERC_AVAILABLE
    void SetWorldRenderer(VKWorldViewRenderer* wr) { m_world_renderer = wr; }
    void SetVKUIRenderer(VKUIRenderer* ui)          { m_ui_renderer = ui; }
#endif

private:
    /** Render thread: record and submit one complete Vulkan frame. */
    void EndFrame_VK();

    bool init_shared_textures();
    void shutdown_shared_textures();

private:
    // ---- VK device + swapchain ----
    VKDevice     m_device;
    VkSurfaceKHR m_surface           = VK_NULL_HANDLE;

    // ---- Sub-renderers (software fallbacks) ----
    VKMapFadePass   m_mapfade;
    VKTextRenderer  m_text_renderer;

    // ---- Dimensions ----
    int  m_width           = 0;
    int  m_height          = 0;
    bool m_needs_resize    = false;
    bool m_frame_begun     = false;

    // Image index acquired in BeginFrame, consumed in EndFrame.
    uint32_t m_current_image_index = 0;
    // Render-thread-safe copy set before Signal() to avoid game-thread races.
    uint32_t m_rt_image_index      = 0;

    // ---- Dummy CPU framebuffer (software rasteriser writes here, ignored) ----

#ifdef RENDERER_VK_SHADERC_AVAILABLE
    // ---- shaderc runtime compiler ----
    shaderc_compiler_t    m_compiler    = nullptr;

    // ---- Shared GPU resources ----
    VKStagingRing         m_staging;
    VKPipelineCache       m_pipelines;
    VKDescriptorLayout    m_descriptors;
    VKTileAtlas*          m_tile_atlas    = nullptr;

    // Palette texture: 256×1 VK_FORMAT_R8G8B8A8_UNORM
    VkImage               m_pal_image     = VK_NULL_HANDLE;
    VmaAllocation         m_pal_alloc     = VK_NULL_HANDLE;
    VkImageView           m_pal_view      = VK_NULL_HANDLE;
    VkSampler             m_pal_sampler   = VK_NULL_HANDLE;
    uint8_t               m_last_palette[768] = {};

    // Fade-table texture: 256×256 VK_FORMAT_R8_UNORM
    VkImage               m_fade_image    = VK_NULL_HANDLE;
    VmaAllocation         m_fade_alloc    = VK_NULL_HANDLE;
    VkImageView           m_fade_view     = VK_NULL_HANDLE;
    VkSampler             m_fade_sampler  = VK_NULL_HANDLE;
    bool                  m_fade_uploaded = false;
    bool                  m_pal_img_ready  = false;   // palette image layout transitioned
    bool                  m_fade_img_ready = false;   // fade image layout transitioned

    // ---- GPU sub-renderers ----
    VKUIRenderer*          m_ui_renderer    = nullptr;  // owned; set by CreateVKUIRenderer()
    VKWorldViewRenderer*   m_world_renderer = nullptr;  // owned; set by CreateVKWorldViewRenderer()

    // ---- Render thread ----
    RenderGraph         m_render_graph;
    RenderThreadManager m_render_thread;
    FrameState          m_rt_frame_state;         // snapshot captured before Signal()
    int                 m_frame_index      = 0;   // ping-pong 0/1 for staging/descriptor
#endif // RENDERER_VK_SHADERC_AVAILABLE
};

/******************************************************************************/

#endif // RENDERERVULKAN_H
