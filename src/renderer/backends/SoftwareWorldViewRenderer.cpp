/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareWorldViewRenderer.cpp
 *     CPU software implementation of IWorldViewRenderer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "bflib_vidraw.h"
#include "engine_render.h"                 // setup_vecs target, software_execute_world_from_ir
#include "renderer/RendererManager.h"      // RendererGetGraphicsWindowPtr / RendererGetLensRenderer
#include "renderer/ILensRenderer.h"        // ResolveWorldCapture* for the possession lens
#include "renderer/software/SwDrawTarget.h" // SwTargetScope — legacy-primitive redirect
#include "post_inc.h"

/******************************************************************************/

void SoftwareWorldViewRenderer::BeginWorldPass(int w, int h, int vp_x, int vp_y,
                                               const TbGraphicsWindow& target)
{
    m_win_x = vp_x;
    m_win_y = vp_y;
    m_win_w = w;
    m_win_h = h;
    // Point the rasteriser at the supplied CPU draw target's graphics-window
    // origin.  The engine no longer hands a pixel pointer in.
    setup_vecs(TbGraphicsWindowOrigin(&target), NULL,
               (unsigned int)target.scanline, (unsigned int)w, (unsigned int)h);
}

void SoftwareWorldViewRenderer::DrawIsometricView()
{
    m_pending   = true;
    m_valid     = true;
    m_frontview = false;
    m_lens      = false;   // set later by MarkDeferredWorldAsLensCapture() if possession
    m_cam       = nullptr;
}

void SoftwareWorldViewRenderer::DrawFrontView(struct Camera* cam)
{
    m_pending   = true;
    m_valid     = true;
    m_frontview = true;
    m_lens      = false;
    m_cam       = cam;
}

void SoftwareWorldViewRenderer::ExecuteRecordedWorld(const TbGraphicsWindow& screen_target)
{
    // Possession lens capture: the lens hands back an off-screen buffer as the
    // world's draw target, then distorts it onto screen_target — no ambient
    // renderer-state push/pop.
    ILensRenderer* lens = m_lens ? RendererGetLensRenderer() : nullptr;
    TbGraphicsWindow world_target = lens ? lens->ResolveWorldCaptureBegin(screen_target)
                                         : screen_target;

    {
        // Point the legacy bflib_vidraw.* world sub-draws (e.g. the possession
        // flame effect) at the same surface as the setup_vecs rasteriser.  For the
        // non-lens case world_target == screen_target, so this is a no-op override.
        SwTargetScope sw_scope(world_target.ptr, world_target.scanline, world_target.screen_height);
        software_execute_world_from_ir(&world_target, m_win_x, m_win_y, m_win_w, m_win_h,
                                       m_frontview ? 1 : 0, m_cam);
    }

    if (lens) lens->ResolveWorldCaptureEnd(screen_target);   // distort -> screen
}

int SoftwareWorldViewRenderer::ResolveDeferredWorld(const TbGraphicsWindow& target)
{
    if (!m_pending)
        return 0;
    m_pending = false;
    ExecuteRecordedWorld(target);
    return 1;
}

void SoftwareWorldViewRenderer::ReexecuteDeferredWorld(const TbGraphicsWindow& target)
{
    if (m_valid)
        ExecuteRecordedWorld(target);
}
