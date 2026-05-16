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
    int m_screenW = 0;
    int m_screenH = 0;

public:
    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;

    // Swipe overlay — software renderer draws sprites directly to WScreen.
    void     DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                              bool draw_lr, int engine_window_x) override;

    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        c.supportsRuntimeSwitch = 1;
        c.supportsMovieCapture  = 1;
        return c;
    }

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;
};

/******************************************************************************/
