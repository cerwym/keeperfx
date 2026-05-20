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
#include "bflib_video.h"       // lbDisplay.DrawFlags (SubmitKeeperHandSprite capture)
#include "engine_render.h"     // process_keeper_sprite_ex, EngineSpriteDrawUsingAlpha
#include "globals.h"
#include <glad/glad.h>
#include "post_inc.h"

// Read at SubmitKeeperHandSprite time to capture alpha state alongside DrawFlags.
extern "C" unsigned char EngineSpriteDrawUsingAlpha;

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
    m_write_cmds->cursor_pointers.Append(cmd);
}

void GLCursorLayer::SubmitKeeperHandSprite(short x, short y,
                                            unsigned short kspr_base,
                                            short angle,
                                            unsigned char sprgroup,
                                            int32_t scale)
{
    if (!m_write_cmds) return;
    IRUICursorKeeperHandCmd cmd;
    cmd.x          = x;
    cmd.y          = y;
    cmd.kspr_base  = kspr_base;
    cmd.angle      = angle;
    cmd.sprgroup   = sprgroup;
    cmd.scale      = scale;
    cmd.draw_flags = lbDisplay.DrawFlags;
    cmd.draw_alpha = EngineSpriteDrawUsingAlpha;
    m_write_cmds->cursor_hands.Append(cmd);
}

void GLCursorLayer::ExecuteCursorFromIR(const UICommandBuffers& cmds)
{
    // ── Pointer sprites (atlas quad path) ────────────────────────────────────
    if (!cmds.cursor_pointers.Empty() && m_glui && m_atlas)
    {
        for (const auto& p : cmds.cursor_pointers)
        {
            SpriteHandle h = m_atlas->GetHandle(p.sprite);
            if (h == kInvalidSpriteHandle) continue;
            m_glui->SubmitCursorPanelSprite((int32_t)p.x, (int32_t)p.y,
                                            p.units_per_px, h);
        }
        m_glui->DrawCursorSprites();
    }

    // ── Keeper-hand sprites (world-view renderer path) ────────────────────────
    if (!cmds.cursor_hands.Empty() && m_wvr)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_wvr->BeginHandSpriteRendering();

        for (const auto& k : cmds.cursor_hands)
        {
            process_keeper_sprite_ex(k.x, k.y, k.kspr_base,
                                     k.angle, k.sprgroup, k.scale,
                                     k.draw_flags, k.draw_alpha);
        }

        m_wvr->EndHandSpriteRendering();  // restores depthMask and disables blend
    }
}

#endif // RENDERER_OPENGL_ENABLED
