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
#include "post_inc.h"

/******************************************************************************/

long SoftwareMapFadePass::StepFadeIn(long step)
{
    return map_fade_in(step);
}

long SoftwareMapFadePass::StepFadeOut(long step)
{
    return map_fade_out(step);
}
