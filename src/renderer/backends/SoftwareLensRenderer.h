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

    /** EndFrame: return the off-screen lens buffer as the draw target the deferred
     *  world should rasterise into (given the on-screen target). */
    TbGraphicsWindow ResolveWorldCaptureBegin(const TbGraphicsWindow& screen_target) override;

    /** EndFrame: distort the captured buffer onto the on-screen target. */
    void ResolveWorldCaptureEnd(const TbGraphicsWindow& screen_target) override;

private:
    // Capture state.  Prepared in the draw phase (BeginWorldCapture), consumed at
    // EndFrame (ResolveWorldCapture*).  Same game thread throughout.
    bool             m_capture_active = false;
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
