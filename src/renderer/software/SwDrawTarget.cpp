/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SwDrawTarget.cpp
 *     Renderer-private current software draw target (see SwDrawTarget.h).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/software/SwDrawTarget.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

/******************************************************************************/

namespace {

// Surface override for the legacy primitives.  When inactive, the accessors fall
// back to the renderer's live framebuffer.  One level of save/restore is kept for
// the (non-nesting) redirect sites.
struct SurfaceOverride {
    TbPixel* base;
    long     pitch;
    long     height;
    bool     active;
};

SurfaceOverride g_cur  = { nullptr, 0, 0, false };
SurfaceOverride g_prev = { nullptr, 0, 0, false };

} // namespace

/******************************************************************************/

extern "C" {

void SwTargetPushSurface(TbPixel* base, long pitch, long height)
{
    g_prev = g_cur;
    g_cur.base   = base;
    g_cur.pitch  = pitch;
    g_cur.height = height;
    g_cur.active = true;
}

void SwTargetPopSurface(void)
{
    g_cur  = g_prev;
    g_prev.active = false;
}

TbPixel* SwTargetWScreen(void)
{
    return g_cur.active ? g_cur.base : RendererGetWScreen();
}

long SwTargetScanline(void)
{
    return g_cur.active ? g_cur.pitch : RendererScreenWidth();
}

long SwTargetScreenHeight(void)
{
    return g_cur.active ? g_cur.height : RendererScreenHeight();
}

// The clip window always tracks the active renderer viewport, independent of any
// surface override.
long SwTargetWindowX(void)      { return RendererGraphicsWindowX(); }
long SwTargetWindowY(void)      { return RendererGraphicsWindowY(); }
long SwTargetWindowWidth(void)  { return RendererGraphicsWindowWidth(); }
long SwTargetWindowHeight(void) { return RendererGraphicsWindowHeight(); }

TbPixel* SwTargetGraphicsWindowPtr(void)
{
    TbPixel* base = SwTargetWScreen();
    if (!base)
        return nullptr;
    return base + SwTargetScanline() * SwTargetWindowY() + SwTargetWindowX();
}

} // extern "C"

/******************************************************************************/
