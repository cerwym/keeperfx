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
#include "post_inc.h"

/******************************************************************************/

void SoftwareWorldViewRenderer::BeginWorldPass(uint8_t* framebuf, int pitch, int w, int h,
                                               int /*vp_x*/, int /*vp_y*/)
{
    setup_vecs(framebuf, NULL, (unsigned int)pitch, (unsigned int)w, (unsigned int)h);
}

void SoftwareWorldViewRenderer::DrawIsometricView()
{
    display_drawlist();
}

void SoftwareWorldViewRenderer::DrawFrontView(struct Camera* cam)
{
    display_fast_drawlist(cam);
}
