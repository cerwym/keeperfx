/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareMapFadePass.h
 *     CPU software implementation of IMapFadePass.
 */
/******************************************************************************/
#pragma once

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

    void PrepareBuffers(uint8_t* fade_src, uint8_t* fade_dest, int scanline, int height) override;
    int32_t StepFadeIn(int32_t step) override;
    int32_t StepFadeOut(int32_t step) override;
 
    const char* GetName() const override { return "SOFTWARE"; }

protected:
    unsigned char* m_map_fade_ghost_table = nullptr;
    unsigned char* m_map_fade_dest = nullptr;
    unsigned char* m_map_fade_src = nullptr;
};

/******************************************************************************/
