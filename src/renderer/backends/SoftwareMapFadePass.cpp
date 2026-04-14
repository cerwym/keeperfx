/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareMapFadePass.cpp
 *     CPU software implementation of IMapFadePass.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "engine_redraw.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

/******************************************************************************/

long SoftwareMapFadePass::StepFadeIn(long step)
{
    long r = map_fade_in(step);
    // map_fade_in writes to lbDisplay.WScreen.  In GL mode the staging buffer
    // is not presented unless explicitly composited; request an overlay blit
    // so the wipe frame is visible.  No-op in software/Vita/3DS modes.
    RendererSubmitStagingOverlay();
    return r;
}

long SoftwareMapFadePass::StepFadeOut(long step)
{
    long r = map_fade_out(step);
    RendererSubmitStagingOverlay();
    return r;
}
