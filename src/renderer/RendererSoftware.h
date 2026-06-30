/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererSoftware.h
 *     Software passthrough renderer backend.
 */
/******************************************************************************/
#pragma once

#include "IRenderer.h"
#include "bflib_video.h"

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
    bool m_lens_capture_active = false;
    unsigned char* m_saved_wscreen = nullptr;
    int m_saved_graphics_w = 0;
    int m_saved_graphics_h = 0;
    TbGraphicsWindow m_saved_viewport = {};
    unsigned char* m_lens_buffer = nullptr;
    unsigned int m_lens_buffer_w = 0;
    unsigned int m_lens_buffer_h = 0;

public:
    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;

    void     NotifyFmvPalette(const uint8_t* bgra_1024) override;
    bool     SubmitTransparentBlit(const uint8_t* buf, int w, int h) override;
    bool     SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                               int dst_x, int dst_y, int dst_w, int dst_h) override;
    void     SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                                int dst_x, int dst_y, int tile_w, int tile_h) override;

    // Swipe overlay — software renderer draws sprites directly to WScreen.
    void     DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                              bool draw_lr, int engine_window_x) override;
    void     BeginLensCapture() override;
    void     EndLensCapture() override;
 
    /** Save the current lbDrawSurface to @p path.
     *  Called while the screen is locked, so lbDrawSurface pixels are valid. */
    bool     ScheduleScreenshot(const char* path, int fmt) override;

    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        c.supportsRuntimeSwitch = 1;
        c.supportsMovieCapture  = 1;
        c.supportsScreenshot    = 1;
        return c;
    }

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;
};

/******************************************************************************/
