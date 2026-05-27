/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IBackend.cpp
 *     Default (CPU) implementations for IBackend.
 * @par Design:
 *     Every virtual method defined here is the authoritative CPU fallback.
 *     GPU subclasses override only the methods they accelerate; the base
 *     transparently provides the CPU path for anything not overridden.
 *
 *     This file was seeded from SoftwareBackend.cpp (which is now a trivial
 *     empty subclass that just supplies a different GetName()).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/IBackend.h"
#include "bflib_video.h"    // lbDisplay, Lb_SPRITE_* flags
#include "bflib_vidraw.h"   // LbSpriteDraw, LbSpriteDrawOneColour, LbSpriteDrawScaledRemap

// ---------------------------------------------------------------------------
// Default (CPU) sprite submission
// ---------------------------------------------------------------------------

TbResult IBackend::SubmitSprite(long x, long y, const struct TbSprite* spr,
                                unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    // Save and restore DrawFlags so the blitter sees the correct per-call flags.
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    TbResult ret = LbSpriteDraw(x, y, spr);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult IBackend::SubmitSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                         unsigned char colour, unsigned int draw_flags)
{
    if (!spr) {
        return Lb_FAIL;
    }
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    TbResult ret = LbSpriteDrawOneColour(x, y, spr, colour);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult IBackend::SubmitSpriteRemap(long x, long y, const struct TbSprite* spr,
                                     const unsigned char* colortable,
                                     unsigned int draw_flags)
{
    if (!spr || !colortable) {
        return Lb_FAIL;
    }
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    TbResult ret = LbSpriteDrawScaledRemap(x, y, spr,
                                           spr->SWidth, spr->SHeight, colortable);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

// ---------------------------------------------------------------------------
// Default frame / sheet / palette lifecycle (all no-ops for the CPU path)
// ---------------------------------------------------------------------------

void IBackend::BeginFrame()   { }
void IBackend::EndFrame()     { }

void IBackend::OnSpriteSheetLoaded(const struct TbSpriteSheet* /*sheet*/) { }
void IBackend::OnSpriteSheetFreed(const struct TbSpriteSheet* /*sheet*/)  { }
void IBackend::OnPaletteSet(const unsigned char* /*palette*/)             { }

const char* IBackend::GetName() const
{
    return "CPU";
}

#include "post_inc.h"
