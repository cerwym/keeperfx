/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareUIRenderer.cpp
 *     Software fallback UI renderer implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#include "engine_render.h"  // process_keeper_sprite
#include "bflib_vidraw.h"   // LbSpriteDrawResized
#include "bflib_sprite.h"   // TbSprite

// For now, these are no-ops since the software renderer
// continues to use the existing CPU staging buffer approach
// The actual rendering happens in engine_render.c through existing code paths

void SoftwareUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth)
{
    // No-op: software renderer uses existing staging buffer approach
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)z_depth;
}

void SoftwareUIRenderer::SubmitKeeperSprite(short x, short y, unsigned short kspr_base,
                                             short angle, unsigned char sprgroup, long scale)
{
    // Software renderer has no frame setup concept — execute immediately.
    process_keeper_sprite(x, y, kspr_base, angle, sprgroup, scale);
}

void SoftwareUIRenderer::SubmitPanelSprite(long x, long y, int units_per_px, SpriteHandle spr, bool flip_horiz)
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

void SoftwareUIRenderer::SubmitScaledSprite(long x, long y, long w, long h, SpriteHandle spr)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    LbSpriteDrawScaled(x, y, it->second, w, h);
}

void SoftwareUIRenderer::SubmitSolidBox(long x, long y, long w, long h, uint8_t color_idx)
{
    LbDrawBox(x, y, w, h, color_idx);
}

uint8_t* SoftwareUIRenderer::AcquireMinimapBuffer(int /*size*/)
{
    // Software mode: caller should write directly to lbDisplay.WScreen.
    return nullptr;
}

void SoftwareUIRenderer::SubmitMinimap(int /*screen_x*/, int /*screen_y*/, int /*size*/)
{
    // No-op: minimap pixels were written directly to lbDisplay.WScreen.
}

void SoftwareUIRenderer::RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr)
{
    m_handle_to_sprite[h] = spr;
}

void SoftwareUIRenderer::Flush()
{
    // No-op: software renderer flushes through existing paths
}

void SoftwareUIRenderer::Clear()
{
    // No-op: software renderer clears through existing paths
}

#include "post_inc.h"