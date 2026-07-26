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
#include "post_inc.h"

/******************************************************************************/

void SoftwareWorldViewRenderer::BeginWorldPass(int w, int h, int vp_x, int vp_y)
{
    m_win_x = vp_x;
    m_win_y = vp_y;
    m_win_w = w;
    m_win_h = h;
    // The renderer owns the CPU framebuffer; point the rasteriser at the current
    // graphics-window origin.  The engine no longer hands a pixel pointer in.
    setup_vecs(RendererGetGraphicsWindowPtr(), NULL,
               (unsigned int)RendererScreenWidth(), (unsigned int)w, (unsigned int)h);
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

void SoftwareWorldViewRenderer::ExecuteRecordedWorld()
{
    // Possession lens capture: wrap the world so it rasterises into the offscreen
    // lens buffer, then distort onto the screen.
    ILensRenderer* lens = m_lens ? RendererGetLensRenderer() : nullptr;
    if (lens) lens->ResolveWorldCaptureBegin();          // WScreen -> lens buffer

    software_execute_world_from_ir(m_win_x, m_win_y, m_win_w, m_win_h,
                                   m_frontview ? 1 : 0, m_cam);

    if (lens) lens->ResolveWorldCaptureEnd();            // distort -> screen
}

int SoftwareWorldViewRenderer::ResolveDeferredWorld()
{
    if (!m_pending)
        return 0;
    m_pending = false;
    ExecuteRecordedWorld();
    return 1;
}

void SoftwareWorldViewRenderer::ReexecuteDeferredWorld()
{
    if (m_valid)
        ExecuteRecordedWorld();
}
