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
#include "renderer/RenderGraph.h"

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
    class ILensRenderer* m_lensRenderer = nullptr;
    class ITextRenderer* m_textRenderer = nullptr;
    class IUIRenderer* m_uiRenderer = nullptr;
    class ICursorLayer* m_cursorLayer = nullptr;
    int m_screenW = 0;
    int m_screenH = 0;

    // ── Software IR executor ──────────────────────────────────────────────────
    // The software backend defers UI + text submissions into this graph during
    // the frame, then replays them (merged by a shared per-frame seq) into
    // lbDrawSurface at EndFrame — "immediate mode emulated through the graph".
    RenderGraph m_render_graph;
    uint32_t    m_frame_seq = 0;   // shared UI+text submission counter, reset each frame
    bool        m_frame_open = false; // guards the double BeginFrame per present (lock + present)

public:
    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;

    bool     PresentImage(const struct RendererPresentImageDesc* desc) override;

    void     NotifyFmvPalette(const uint8_t* bgra_1024) override;
    bool     SubmitTransparentBlit(const uint8_t* buf, int w, int h) override;
    bool     SubmitLandviewZoom(const uint8_t* src_buf, int src_w, int src_h,
                                float center_map_x, float center_map_y,
                                float screen_cx, float screen_cy,
                                float scale) override;
    bool     DrawLandviewFrame(const struct TbHugeSprite* spr, long sp_len,
                               int xshift, int yshift, int units_per_px) override;
    bool     SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                               int dst_x, int dst_y, int dst_w, int dst_h) override;
    void     SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                                int dst_x, int dst_y, int tile_w, int tile_h) override;

    // Swipe overlay — software renderer draws sprites directly to WScreen.
    void     DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                              bool draw_lr, int engine_window_x) override;
 
    /** Save the current lbDrawSurface to @p path.
     *  Called while the screen is locked, so lbDrawSurface pixels are valid. */
    bool     ScheduleScreenshot(const char* path, int fmt) override;

    const char* GetName() const override;

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        c.supportsMovieCapture  = 1;
        return c;
    }

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ILensRenderer* GetLensRenderer() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;
    ICursorLayer* GetCursorLayer() override;
};

/******************************************************************************/
