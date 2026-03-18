/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareMapFadePass.h
 *     CPU software implementation of IMapFadePass.
 */
/******************************************************************************/
#ifndef SOFTWARE_MAP_FADE_PASS_H
#define SOFTWARE_MAP_FADE_PASS_H

#include "renderer/IMapFadePass.h"

/******************************************************************************/

/**
 * CPU software implementation of IMapFadePass.
 *
 * Delegates to existing C engine_redraw functions unchanged:
 *   StepFadeIn(step)  → engine_redraw_map_fade_in(step)
 *   StepFadeOut(step) → engine_redraw_map_fade_out(step)
 */
class SoftwareMapFadePass : public IMapFadePass {
public:
    SoftwareMapFadePass()  = default;
    ~SoftwareMapFadePass() = default;

    long StepFadeIn(long step) override;
    long StepFadeOut(long step) override;

    const char* GetName() const override { return "SOFTWARE"; }
};

/******************************************************************************/
#endif // SOFTWARE_MAP_FADE_PASS_H
