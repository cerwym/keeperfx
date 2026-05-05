/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLCursorLayer.cpp
 *     OpenGL implementation of ICursorLayer.
 *
 *     Pointer sprites are submitted through the existing GLUIRenderer atlas
 *     path (SubmitPanelSprite → DrawCursorSprites), so they share the same
 *     codec as every other UI sprite.  Keeper-hand sprites are rendered via
 *     GLWorldViewRenderer::BeginHandSpriteRendering and process_keeper_sprite,
 *     exactly as the old GLUIRenderer::DrawHandSprites did.
 *
 *     Both are drawn in a single Draw() call as the absolute last step
 *     before platform_swap_gl_buffers(), so neither is ever tinted or obscured.
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
    if (!spr) return;
    PendingPointerSprite p;
    p.spr         = spr;
    p.x           = x;
    p.y           = y;
    p.units_per_px = units_per_px;
    m_pointers.push_back(p);
}

void GLCursorLayer::SubmitKeeperHandSprite(short x, short y,
                                            unsigned short kspr_base,
                                            short angle,
                                            unsigned char sprgroup,
                                            int32_t scale)
{
    PendingKeeperSprite k;
    k.x          = x;
    k.y          = y;
    k.kspr_base  = kspr_base;
    k.angle      = angle;
    k.sprgroup   = sprgroup;
    k.scale      = scale;
    k.draw_flags = lbDisplay.DrawFlags;
    k.draw_alpha = EngineSpriteDrawUsingAlpha;
    m_keepers.push_back(k);
}

void GLCursorLayer::Draw()
{
    // ── Pointer sprites (atlas quad path) ────────────────────────────────────
    // GLUIRenderer::DrawFront() has already run, so m_ui_quads is empty.
    // Submitting here and calling DrawCursorSprites() targets only these quads.
    if (!m_pointers.empty() && m_glui && m_atlas)
    {
        for (const auto& p : m_pointers)
        {
            SpriteUV uv;
            SpriteHandle h = m_atlas->GetHandle(p.spr);
            if (h == kInvalidSpriteHandle) continue;
            if (!m_atlas->GetUV(h, uv)) continue;
            m_glui->SubmitPanelSprite((int32_t)p.x, (int32_t)p.y,
                                      p.units_per_px, h);
        }
        m_glui->DrawCursorSprites();
    }

    // ── Keeper-hand sprites (world-view renderer path) ────────────────────────
    if (!m_keepers.empty() && m_wvr)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_wvr->BeginHandSpriteRendering();

        for (const auto& k : m_keepers)
        {
            process_keeper_sprite_ex(k.x, k.y, k.kspr_base,
                                     k.angle, k.sprgroup, k.scale,
                                     k.draw_flags, k.draw_alpha);
        }

        m_wvr->EndHandSpriteRendering();
        glDisable(GL_BLEND);
    }
}

void GLCursorLayer::Clear()
{
    m_pointers.clear();
    m_keepers.clear();
}

#endif // RENDERER_OPENGL_ENABLED
