/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SWCursorLayer.cpp
 *     Software (CPU) implementation of ICursorLayer.
 *
 *     The pointer sprite is drawn into RendererGetWScreen() immediately in
 *     Flush() — no backup/restore is needed because WScreen is fully
 *     rebuilt from scratch on every frame before EndFrame() is called.
 *
 *     Keeper-hand sprites call process_keeper_sprite() immediately at submit
 *     time (same as the old IUIRenderer::SubmitKeeperSprite default).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "bflib_mouse.h"
#include "renderer/backends/SWCursorLayer.h"

#include "bflib_basics.h"
#include "bflib_sprite.h"      // TbSprite
#include "bflib_sprfnt.h"      // scale_ui_value_lofi
#include "bflib_vidraw.h"      // LbSpriteDrawUsingScalingUpDataSolidLR, LbSpriteSetScaling*
#include "engine_render.h"     // process_keeper_sprite_ex
#include "globals.h"
#include "renderer/RendererManager.h"
#include "renderer/ir/UICommands.h"   // UICommandBuffers, IRUICursorKeeperHandCmd

#include "post_inc.h"

/******************************************************************************/

// ---------------------------------------------------------------------------
// Internal sprite scaler state (was lbPointerAdvancedDraw/cursor_*steps* in
// bflib_mspointer.cpp; now private to the cursor layer).
// ---------------------------------------------------------------------------

#define SW_CURSOR_XSTEPS (MAX_SUPPORTED_SCREEN_WIDTH  / 10)
#define SW_CURSOR_YSTEPS (MAX_SUPPORTED_SCREEN_HEIGHT / 10)

static int32_t s_xsteps[2 * SW_CURSOR_XSTEPS];
static int32_t s_ysteps[2 * SW_CURSOR_YSTEPS];

static void set_scaling_w_clipped(long x, long sw, long dw, long gw)
{
    if (sw > SW_CURSOR_XSTEPS) sw = SW_CURSOR_XSTEPS;
    LbSpriteSetScalingWidthClippedArray(s_xsteps, x, sw, dw, gw);
}

static void set_scaling_w_simple(long x, long sw, long dw)
{
    if (sw > SW_CURSOR_XSTEPS) sw = SW_CURSOR_XSTEPS;
    LbSpriteSetScalingWidthSimpleArray(s_xsteps, x, sw, dw);
}

static void set_scaling_h_clipped(long y, long sh, long dh, long gh)
{
    if (sh > SW_CURSOR_YSTEPS) sh = SW_CURSOR_YSTEPS;
    LbSpriteSetScalingHeightClippedArray(s_ysteps, y, sh, dh, gh);
}

static void set_scaling_h_simple(long y, long sh, long dh)
{
    if (sh > SW_CURSOR_YSTEPS) sh = SW_CURSOR_YSTEPS;
    LbSpriteSetScalingHeightSimpleArray(s_ysteps, y, sh, dh);
}

/** Draw cursor sprite directly into outbuf (RendererGetWScreen()). */
static void draw_pointer_sprite(int32_t x, int32_t y, const TbSprite* spr,
                                 TbPixel* outbuf, unsigned long scanline)
{
    int dw = scale_ui_value_lofi(spr->SWidth);
    int dh = scale_ui_value_lofi(spr->SHeight);
    if (dw <= 0 || dh <= 0) return;
    if (lbMouse.MouseWindowWidth <= 0 || lbMouse.MouseWindowHeight <= 0) return;

    if (x < 0 || (dw + spr->SWidth + x) >= lbMouse.MouseWindowWidth)
        set_scaling_w_clipped(x, spr->SWidth, dw, lbMouse.MouseWindowWidth);
    else
        set_scaling_w_simple(x, spr->SWidth, dw);

    if (y < 0 || (dh + spr->SHeight + y) >= lbMouse.MouseWindowHeight)
        set_scaling_h_clipped(y, spr->SHeight, dh, lbMouse.MouseWindowHeight);
    else
        set_scaling_h_simple(y, spr->SHeight, dh);

    outbuf = &outbuf[s_xsteps[0] + scanline * s_ysteps[0]];
    const struct TbSourceBuffer buf = {
        spr->Data, spr->SWidth, spr->SHeight, spr->SWidth,
    };
    LbSpriteDrawUsingScalingUpDataSolidLR(outbuf, scanline,
                                           lbMouse.MouseWindowHeight,
                                           s_xsteps, s_ysteps, &buf);
}

/******************************************************************************/

void SWCursorLayer::SubmitPointerSprite(const TbSprite* spr, int32_t x, int32_t y, int /*units_per_px*/)
{
    m_pointer_spr = spr;
    m_pointer_x   = x;
    m_pointer_y   = y;
}

void SWCursorLayer::SubmitKeeperHandSprite(short x, short y,
                                           unsigned short kspr_base,
                                           short angle,
                                           unsigned char sprgroup,
                                           int32_t scale,
                                           TbDrawFlagsMask draw_flags)
{
    if (m_write_cmds)
    {
        IRUICursorKeeperHandCmd cmd;
        cmd.x = x; cmd.y = y;
        cmd.kspr_base = kspr_base; cmd.angle = angle; cmd.sprgroup = sprgroup;
        cmd.scale = scale; cmd.draw_flags = draw_flags;
        cmd.seq = m_write_cmds->NextSeq();
        m_write_cmds->cursor_hands.Append(cmd);
        return;
    }
    // No write window: draw immediately (legacy fallback).
    process_keeper_sprite_ex(x, y, kspr_base, angle, sprgroup, scale, draw_flags,
                             (draw_flags & Lb_SPRITE_ALPHA_ADDITIVE) != 0);
}

void SWCursorLayer::Draw(const TbGraphicsWindow& target)
{
    // Draw the pointer sprite into WScreen right before the SDL blit.
    // WScreen is fully rebuilt each frame so no backup/restore is needed.
    if (m_pointer_spr && target.ptr)
    {
        draw_pointer_sprite(m_pointer_x, m_pointer_y,
                            m_pointer_spr,
                            target.ptr,
                            (unsigned long)target.scanline);
    }
}

void SWCursorLayer::Clear()
{
    m_pointer_spr = nullptr;
    m_pointer_x   = 0;
    m_pointer_y   = 0;
}
