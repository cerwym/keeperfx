/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareLensRenderer.h
 *     CPU software implementation of ILensRenderer.
 * @par Purpose:
 *     The software backend has no GPU lens passes — it owns the immediate-mode
 *     world-capture bracket instead. BeginWorldCapture() redirects WScreen to an
 *     offscreen lens buffer around engine()+swipe (so only the world is captured,
 *     UI excluded); EndWorldCapture() restores WScreen and distorts the captured
 *     buffer back onto the screen via draw_lens_effect() -> the CPU distortion
 *     loops. This logic previously lived on RendererSoftware; it now lives here so
 *     each backend fully owns its lens realization and the lens system is reached
 *     uniformly through RendererGetLensRenderer(). Software has no GPU passes —
 *     the immediate-mode capture bracket is the whole realization here.
 */
/******************************************************************************/
#pragma once

#include "renderer/ILensRenderer.h"
#include "bflib_video.h"   // TbGraphicsWindow

/******************************************************************************/

class SoftwareLensRenderer : public ILensRenderer {
public:
    SoftwareLensRenderer()  = default;
    ~SoftwareLensRenderer() override = default;

    const char* GetName() const override { return "SOFTWARE"; }

    /** Redirect WScreen to the offscreen lens buffer so engine()+swipe render
     *  the world (UI excluded) into it. No-op if no lens effect is ready. */
    void BeginWorldCapture() override;

    /** Restore WScreen and distort the captured world buffer back onto the
     *  screen via draw_lens_effect(). No-op if BeginWorldCapture() did not
     *  activate a capture this frame. */
    void EndWorldCapture() override;

    // No GPU passes on software — the capture bracket above is the whole path.

private:
    // Immediate-mode capture bracket state (game thread == render thread here).
    bool             m_capture_active = false;
    unsigned char*   m_saved_wscreen  = nullptr;
    int              m_saved_graphics_w = 0;
    int              m_saved_graphics_h = 0;
    TbGraphicsWindow m_saved_viewport = {};
    unsigned char*   m_lens_buffer    = nullptr;
    unsigned int     m_lens_buffer_w  = 0;
    unsigned int     m_lens_buffer_h  = 0;
};

/******************************************************************************/
