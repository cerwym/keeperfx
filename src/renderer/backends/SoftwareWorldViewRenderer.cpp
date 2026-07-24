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
#include "engine_render.h"
#include "renderer/RendererManager.h"   // RendererGetGraphicsWindowPtr / RendererScreenWidth
#include "post_inc.h"

/******************************************************************************/

void SoftwareWorldViewRenderer::BeginWorldPass(int w, int h, int /*vp_x*/, int /*vp_y*/)
{
    // The renderer owns the CPU framebuffer; point the rasteriser at the current
    // graphics-window origin.  The engine no longer hands a pixel pointer in.
    setup_vecs(RendererGetGraphicsWindowPtr(), NULL,
               (unsigned int)RendererScreenWidth(), (unsigned int)w, (unsigned int)h);
}

void SoftwareWorldViewRenderer::DrawIsometricView()
{
    display_drawlist();
}

void SoftwareWorldViewRenderer::DrawFrontView(struct Camera* cam)
{
    display_fast_drawlist(cam);
}
