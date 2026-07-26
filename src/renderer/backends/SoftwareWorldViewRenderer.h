/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareWorldViewRenderer.h
 *     CPU software implementation of IWorldViewRenderer.
 */
/******************************************************************************/
#pragma once

#include "renderer/IWorldViewRenderer.h"

/******************************************************************************/

class SoftwareWorldViewRenderer : public IWorldViewRenderer {
public:
    SoftwareWorldViewRenderer()  = default;
    ~SoftwareWorldViewRenderer() = default;

    void BeginWorldPass(int w, int h,
                        int vp_x, int vp_y) override;
    void DrawIsometricView() override;
    void DrawFrontView(struct Camera* cam) override;

    int  ResolveDeferredWorld() override;
    void ReexecuteDeferredWorld() override;
    void MarkDeferredWorldAsLensCapture() override { m_lens = true; }

    const char* GetName() const override { return "SOFTWARE"; }

private:
    // Rasterise the recorded descriptor into the current WScreen, routing through
    // the lens buffer + distort when m_lens is set.
    void ExecuteRecordedWorld();

    // Engine-window rect of the current pass,
    int  m_win_x = 0;
    int  m_win_y = 0;
    int  m_win_w = 0;
    int  m_win_h = 0;

    // Deferred-world descriptor recorded by DrawIsometricView/DrawFrontView.
    bool m_pending   = false;  // a world was recorded this frame, not yet resolved
    bool m_valid     = false;  // a world has been recorded at least once (re-execute)
    bool m_frontview = false;  // front view vs isometric/1st-person
    bool m_lens      = false;  // possession lens capture (route through lens buffer)
    struct Camera* m_cam = nullptr; // front-view camera (null for iso)
};

/******************************************************************************/
