/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.cpp
 *     Default (CPU) implementations for IUIRenderer.
 * @par Design:
 *     Every virtual method defined here is the authoritative CPU fallback.
 *     GPU subclasses override only the methods they accelerate; they never need
 *     to re-implement the CPU path because the base already provides it.
 *
 *     This file was seeded from SoftwareUIRenderer.cpp (which is now a trivial
 *     empty subclass that just supplies a different GetName()).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/IUIRenderer.h"
#include "engine_render.h"  // process_keeper_sprite
#include "bflib_vidraw.h"   // LbSpriteDrawResized, LbSpriteDrawScaled, LbDrawBox, LbSpriteDrawScaledRemap
#include "bflib_sprite.h"   // TbSprite
#include "vidmode.h"         // pixmap (TbColorTables) for fade_tables

// ---------------------------------------------------------------------------
// Sprite handle registry
// ---------------------------------------------------------------------------

void IUIRenderer::RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr)
{
    m_handle_to_sprite[h] = spr;
}

// ---------------------------------------------------------------------------
// Default (CPU) submission implementations
// ---------------------------------------------------------------------------

void IUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2,
                                     unsigned char color, float z_depth)
{
    // Software renderer draws selectors through the staging buffer directly;
    // the GPU path overrides this with a line-draw quad.
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)z_depth;
}

void IUIRenderer::SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                     short angle, unsigned char sprgroup, long scale)
{
    // Software renderer has no frame-setup concept — execute immediately.
    process_keeper_sprite(x, y, kspr_base, angle, sprgroup, scale);
}

void IUIRenderer::SubmitPanelSprite(long x, long y, int units_per_px,
                                    SpriteHandle spr, bool flip_horiz)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    if (flip_horiz)
    {
        unsigned short saved = lbDisplay.DrawFlags;
        lbDisplay.DrawFlags = (saved | Lb_SPRITE_FLIP_HORIZ);
        LbSpriteDrawResized(x, y, units_per_px, it->second);
        lbDisplay.DrawFlags = saved;
    }
    else
    {
        LbSpriteDrawResized(x, y, units_per_px, it->second);
    }
}

void IUIRenderer::SubmitPanelSpriteRemap(long x, long y, int units_per_px,
                                         SpriteHandle spr, int remap_row)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    LbSpriteDrawResizedRemap(x, y, units_per_px, it->second,
                             &pixmap.fade_tables[remap_row * 256]);
}

void IUIRenderer::SubmitPanelSpriteColored(long x, long y, int units_per_px,
                                           SpriteHandle spr, uint8_t color_idx)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    LbSpriteDrawResizedOneColour(x, y, units_per_px, it->second, color_idx);
}

void IUIRenderer::SubmitScaledSprite(long x, long y, long w, long h,
                                     SpriteHandle spr)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    LbSpriteDrawScaled(x, y, it->second, w, h);
}

void IUIRenderer::SubmitSolidBox(long x, long y, long w, long h, uint8_t color_idx)
{
    LbDrawBox(x, y, w, h, color_idx);
}

void IUIRenderer::SubmitSolidBoxAlpha(long x, long y, long w, long h, uint8_t color_idx, float alpha)
{
    // CPU fallback: use GlassMap blending to approximate the requested transparency.
    unsigned short saved_flags = lbDisplay.DrawFlags;
    if (alpha < 0.75f)
        lbDisplay.DrawFlags |= Lb_SPRITE_TRANSPAR4;
    else if (alpha < 0.9f)
        lbDisplay.DrawFlags |= Lb_SPRITE_TRANSPAR8;
    LbDrawBox(x, y, w, h, color_idx);
    lbDisplay.DrawFlags = saved_flags;
}

uint8_t* IUIRenderer::AcquireMinimapBuffer(int /*size*/)
{
    // CPU mode: caller writes directly to lbDisplay.WScreen.
    return nullptr;
}

void IUIRenderer::SubmitMinimap(int /*screen_x*/, int /*screen_y*/, int /*size*/)
{
    // No-op: minimap pixels were written directly to lbDisplay.WScreen.
}

void IUIRenderer::Flush()
{
    // No-op: CPU path flushes through existing staging-buffer blit.
}

void IUIRenderer::Clear()
{
    // No-op: CPU path clears through existing paths.
}

const char* IUIRenderer::GetName() const
{
    return "CPU_UI";
}

#include "post_inc.h"
