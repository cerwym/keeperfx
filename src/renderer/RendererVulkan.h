/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererVulkan.h
 *     Vulkan renderer backend (scaffold).
 * @par Purpose:
 *     Stub IRenderer implementation that creates a Vulkan context via SDL2
 *     and presents black frames.  All actual IR-driven rendering is deferred
 *     to a later branch.  Sub-renderers fall through to software defaults so
 *     audio, input and game logic run normally.
 */
/******************************************************************************/
#pragma once
#ifndef RENDERERVULKAN_H
#define RENDERERVULKAN_H

#include "renderer/IRenderer.h"
#include "renderer/vulkan/VKDevice.h"
#include <vulkan/vulkan.h>
#include <vector>

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
    const char*        GetName()              const override;
    bool               SupportsRuntimeSwitch() const override;
    BackendCapabilities GetCapabilities()      const override;

    // ---- Sub-renderer access (all nullptr — fall through to software defaults) ----
    IWorldViewRenderer* GetWorldViewRenderer() override { return nullptr; }
    IMapFadePass*       GetMapFadePass()        override { return nullptr; }
    ITextRenderer*      GetTextRenderer()       override { return nullptr; }
    IUIRenderer*        GetUIRenderer()         override { return nullptr; }

private:
    bool RecreateSwapchain();

    VKDevice             m_device;
    VkSurfaceKHR         m_surface       = VK_NULL_HANDLE;

    // Dummy CPU framebuffer — written by the software rasteriser, ignored.
    std::vector<uint8_t> m_dummy_fb;
    int                  m_dummy_pitch   = 0;

    // Cached frame dimensions.
    int                  m_width         = 0;
    int                  m_height        = 0;

    // Image index acquired in BeginFrame, consumed in EndFrame.
    uint32_t             m_current_image_index = 0;

    // Pending swapchain-out-of-date flag set by BeginFrame / EndFrame.
    bool                 m_needs_resize  = false;
};

#endif // RENDERERVULKAN_H
