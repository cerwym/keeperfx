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

int32_t SoftwareMapFadePass::StepFadeIn(int32_t step)
{
    return map_fade_in(step);
}

int32_t SoftwareMapFadePass::StepFadeOut(int32_t step)
{
    return map_fade_out(step);
}
