/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareLensRenderer.h
 *     CPU software implementation of ILensRenderer.
 * @par Purpose:
 *     The software backend has no GPU lens passes — it owns a CPU world-capture
 *     instead. 
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

    /** Draw phase: set up the full-screen engine window (so engine() computes the
     *  lens geometry) and snapshot the distort params.  Does NOT redirect WScreen
     *  — the world is deferred to EndFrame.  Returns true if a capture is active. */
    bool BeginWorldCapture() override;

    bool IsWorldCaptureActive() const override { return m_capture_active; }

    /** EndFrame: redirect WScreen to the offscreen lens buffer for the deferred
     *  world + swipe. */
    void ResolveWorldCaptureBegin() override;

    /** EndFrame: distort the captured buffer onto the screen and restore WScreen. */
    void ResolveWorldCaptureEnd() override;

private:
    // Capture state.  Prepared in the draw phase (BeginWorldCapture), consumed at
    // EndFrame (ResolveWorldCapture*).  Same game thread throughout.
    bool             m_capture_active = false;
    unsigned char*   m_saved_wscreen  = nullptr;
    int              m_saved_graphics_w = 0;
    int              m_saved_graphics_h = 0;
    TbGraphicsWindow m_saved_viewport = {};
    unsigned char*   m_lens_buffer    = nullptr;
    unsigned int     m_lens_buffer_w  = 0;
    unsigned int     m_lens_buffer_h  = 0;
    // Distort params snapshotted at prepare time (engine window may change before
    // EndFrame as later UI draws run).
    long             m_view_x = 0;
    long             m_view_y = 0;
    long             m_view_width = 0;
    long             m_view_height = 0;
    int              m_lens_type = 0;
};

/******************************************************************************/
