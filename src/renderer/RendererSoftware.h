/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererSoftware.h
 *     Software passthrough renderer backend.
 */
/******************************************************************************/
#pragma once

#include "IRenderer.h"

/******************************************************************************/

/**
 * Software passthrough renderer backend.
 *
 * Provides identical behaviour to the original SDL2 rendering path:
 * - LockFramebuffer() locks the SDL draw surface and returns its pixel pointer
 * - UnlockFramebuffer() unlocks the draw surface
 * - EndFrame() blits the draw surface to the screen surface and calls SDL_UpdateWindowSurface
 *
 * This backend is the zero-risk baseline — the game renders exactly as before.
 */
class RendererSoftware : public IRenderer {
private:
    class IWorldViewRenderer* m_worldViewRenderer = nullptr;
    class IMapFadePass* m_mapFadePass = nullptr;
    class ITextRenderer* m_textRenderer = nullptr;
    class IUIRenderer* m_uiRenderer = nullptr;

public:
    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;
    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;
};

/******************************************************************************/
