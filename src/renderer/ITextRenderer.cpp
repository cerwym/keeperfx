/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ITextRenderer.cpp
 *     Shared implementation for ITextRenderer protected helpers.
 *     Phase 1: stubs — no callers yet.
 *     Phase 3+: TextLayout() and justification helpers populated from
 *               bflib_sprfnt.c originals.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/ITextRenderer.h"
#include "post_inc.h"

/******************************************************************************/

void ITextRenderer::TextLayout(const TextLayoutContext& /*ctx*/,
                               int32_t /*posx*/, int32_t /*posy*/,
                               int32_t /*units_per_px*/,
                               const char* /*text*/,
                               TextSegmentFn /*draw_fn*/, void* /*userdata*/)
{
    // Phase 1 stub — will be populated in Phase 3 from LbTextLayout().
}

bool ITextRenderer::AlignMethodSet(uint16_t /*flags*/)
{
    return false;
}

int32_t ITextRenderer::JustifiedCharPosX(int32_t startx, int32_t /*all_width*/,
                                         int32_t /*spr_width*/,
                                         int32_t /*mul_width*/,
                                         uint16_t /*flags*/)
{
    return startx;
}

int32_t ITextRenderer::JustifiedCharPosY(int32_t starty, int32_t /*all_height*/,
                                         int32_t /*spr_height*/,
                                         uint16_t /*flags*/)
{
    return starty;
}

int32_t ITextRenderer::JustifiedCharWidth(int32_t /*all_width*/, int32_t spr_width,
                                          int32_t /*word_count*/,
                                          int32_t /*units_per_px*/,
                                          uint16_t /*flags*/)
{
    return spr_width;
}
