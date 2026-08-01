/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLCursorLayer.cpp
 *     OpenGL implementation of ICursorLayer.
 *
 *     Cursor data flows through the shared UICommandBuffers IR path:
 *
 *       Game thread: SubmitPointerSprite / SubmitKeeperHandSprite
 *         → emit IRUICursorPointerCmd / IRUICursorKeeperHandCmd into the
 *           write-side UICommandBuffers::cursor_pointers / cursor_hands.
 *
 *       Render thread: ExecuteCursorFromIR(cmds)
 *         → reads from the read-side UICommandBuffers (after RenderGraph::Flip)
 *           and draws with the same GL paths as before:
 *             Pointer sprites: atlas quad via GLUIRenderer::DrawCursorSprites
 *             Keeper sprites:  process_keeper_sprite_ex via GLWorldViewRenderer
 *
 *     Thread safety: the write/read sides are swapped atomically by
 *     RenderGraph::Flip() (inside RendererOpenGL::EndFrame()) so no additional
 *     locking is needed here.
 */
/******************************************************************************/
#ifdef RENDERER_OPENGL_ENABLED

#include "pre_inc.h"
#include "renderer/opengl/GLCursorLayer.h"
#include "renderer/opengl/GLUIRenderer.h"
#include "renderer/opengl/GLWorldViewRenderer.h"
#include "renderer/opengl/GLSpriteAtlas.h"
#include "engine_render.h"     // process_keeper_sprite_ex
#include "globals.h"
#include <glad/glad.h>
#include "post_inc.h"

/******************************************************************************/

void GLCursorLayer::SubmitPointerSprite(const TbSprite* spr,
                                        int32_t x, int32_t y,
                                        int units_per_px)
{
    if (!spr || !m_write_cmds) return;
    IRUICursorPointerCmd cmd;
    cmd.sprite       = spr;
    cmd.x            = x;
    cmd.y            = y;
    cmd.units_per_px = units_per_px;
    cmd.draw_flags   = 0;
    m_write_cmds->cursor_pointers.Append(cmd);
}

void GLCursorLayer::SubmitKeeperHandSprite(short x, short y,
                                            unsigned short kspr_base,
                                            short angle,
                                            unsigned char sprgroup,
                                            int32_t scale,
                                            TbDrawFlagsMask draw_flags)
{
    if (!m_wvr) return;
    // Pre-compute the sprite on the game thread: process_keeper_sprite_ex resolves
    // all game-global state (water offsets, thing flags, remap table, etc.) and
    // routes through SubmitKeeperSprite() which, while m_cursor_capture is active,
    // stores the result as IRWorldKeeperSpriteCmd in m_cursor_kspr_ir.
    // FlipBuffers() then moves that to m_rt_cursor_kspr_ir for the render thread,
    // which calls DrawCursorKeeperSprites() — no game globals are ever touched there.
    m_wvr->BeginCursorCapture();
    process_keeper_sprite_ex(x, y, kspr_base, angle, sprgroup, scale,
                             draw_flags, (draw_flags & Lb_SPRITE_ALPHA_ADDITIVE) != 0);
    m_wvr->EndCursorCapture();
}

void GLCursorLayer::ExecuteCursorFromIR(const UICommandBuffers& cmds, const TbGraphicsWindow& /*target*/)
{
    // ── Pointer sprites (atlas quad path) ────────────────────────────────────
    if (!cmds.cursor_pointers.Empty() && m_glui && m_atlas)
    {
        for (const auto& p : cmds.cursor_pointers)
        {
            SpriteHandle h = m_atlas->GetHandle(p.sprite);
            if (h == kInvalidSpriteHandle) continue;
            m_glui->SubmitCursorPanelSprite((int32_t)p.x, (int32_t)p.y,
                                            p.units_per_px, h, p.draw_flags);
        }
        m_glui->DrawCursorSprites();
    }

    // ── Keeper-hand sprites (pre-computed on game thread) ─────────────────────
    if (m_wvr)
        m_wvr->DrawCursorKeeperSprites();
}

#endif // RENDERER_OPENGL_ENABLED
