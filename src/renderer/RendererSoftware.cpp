/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererSoftware.cpp
 *     Software passthrough renderer backend implementation.
 * @par Purpose:
 *     Wraps the original SDL2-based rendering path so it satisfies the
 *     IRenderer interface. Behaviour is byte-for-byte identical to the
 *     pre-abstraction code path.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererSoftware.h"

#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"

#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "bflib_render.h"
#include "bflib_sprite.h"    // TbSpriteSheet, get_sprite
#include "bflib_vidraw.h"    // LbSpriteDrawResized
#include "renderer/RendererManager.h"

#include <cstring>
#include "post_inc.h"

/******************************************************************************/

bool RendererSoftware::Init()
{
    // Obtain the window surface and create intermediate scale surface if needed.
    if (LbScreenSetupRendererSurfaces() != Lb_SUCCESS)
        return false;

    // Initialize transparency mapping tables for sprite rendering
    if (render_ghost == nullptr) {
        render_ghost = static_cast<unsigned char*>(KfxAlloc(65536)); // 256x256 table
        if (render_ghost == nullptr) {
            ERRORLOG("RendererSoftware: Failed to allocate render_ghost transparency table");
            return false;
        }
        // Initialize with default transparency mapping (similar to GlassMap)
        // This creates a 50% transparency effect
        for (int i = 0; i < 256; i++) {
            for (int j = 0; j < 256; j++) {
                render_ghost[i * 256 + j] = (i + j) / 2;
            }
        }
    }

    if (render_alpha == nullptr) {
        render_alpha = static_cast<unsigned char*>(KfxAlloc(65536)); // 256x256 table
        if (render_alpha == nullptr) {
            ERRORLOG("RendererSoftware: Failed to allocate render_alpha transparency table");
            KfxFree(render_ghost);
            render_ghost = nullptr;
            return false;
        }
        // Initialize with alpha blending transparency mapping
        // This creates an additive alpha blending effect
        for (int i = 0; i < 256; i++) {
            for (int j = 0; j < 256; j++) {
                int result = i + (j * 3) / 4; // 75% source, 25% destination
                render_alpha[i * 256 + j] = (result > 255) ? 255 : result;
            }
        }
    }

    // Initialize sub-renderers
    m_worldViewRenderer = new SoftwareWorldViewRenderer();
    m_mapFadePass = new SoftwareMapFadePass();
    m_textRenderer = new SoftwareTextRenderer();
    m_uiRenderer = new SoftwareUIRenderer();

    return true;
}

void RendererSoftware::Shutdown()
{
    // Free transparency mapping tables
    if (render_ghost) {
        KfxFree(render_ghost);
        render_ghost = nullptr;
    }

    if (render_alpha) {
        KfxFree(render_alpha);
        render_alpha = nullptr;
    }

    LbScreenReleaseRendererSurfaces();

    // Clean up sub-renderers
    delete m_worldViewRenderer;
    m_worldViewRenderer = nullptr;
    delete m_mapFadePass;
    m_mapFadePass = nullptr;
    delete m_textRenderer;
    m_textRenderer = nullptr;
    delete m_uiRenderer;
    m_uiRenderer = nullptr;
}

bool RendererSoftware::BeginFrame()
{
    m_screenW = lbDisplay.PhysicalScreenWidth;
    m_screenH = lbDisplay.PhysicalScreenHeight;
    CursorLayer_Clear();
    return true;
}

void RendererSoftware::EndFrame()
{
    // Draw the cursor into WScreen before blitting to the window.
    CursorLayer_Draw();
    LbScreenSwap();
}

void RendererSoftware::ClearScreen(uint8_t colour_index)
{
    LbScreenClearIndex(colour_index);
}

uint8_t* RendererSoftware::LockFramebuffer(int* out_pitch)
{
    return LbScreenGetPixels(out_pitch);
}

void RendererSoftware::UnlockFramebuffer()
{
    LbScreenReleasePixels();
}

void RendererSoftware::DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                                        bool draw_lr, int engine_window_x)
{
    static const int SPRITES_X = 3;
    static const int SPRITES_Y = 2;

    const struct TbSprite* sprlist = get_sprite(sprites, SPRITES_X * SPRITES_Y * frame);
    if (!sprlist)
        return;

    const struct TbSprite* startspr = &sprlist[1];
    const struct TbSprite* endspr   = &sprlist[1];
    long allwidth = 0;
    for (int n = 0; n < SPRITES_X; n++)
    {
        allwidth += endspr->SWidth;
        endspr++;
    }
    int units_per_px = (RendererPhysicalWidth() * 59 / 64) * 16 / allwidth;
    int scrpos_y = (m_screenH * 16 / units_per_px - (startspr->SHeight + endspr->SHeight)) / 2;

    if (draw_lr)
    {
        lbDisplay.DrawFlags = Lb_SPRITE_TRANSPAR4;
        int delta_y = sprlist[1].SHeight;
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &startspr[i];
            int scrpos_x = ((m_screenW + (2 * engine_window_x)) * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                scrpos_x += spr->SWidth;
                spr++;
            }
            scrpos_y += delta_y;
        }
    }
    else
    {
        lbDisplay.DrawFlags = Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_FLIP_HORIZ;
        for (int i = 0; i < SPRITES_X * SPRITES_Y; i += SPRITES_X)
        {
            const struct TbSprite* spr = &sprlist[SPRITES_X + i];
            int delta_y = spr->SHeight;
            int scrpos_x = (m_screenW * 16 / units_per_px - allwidth) / 2;
            for (int n = 0; n < SPRITES_X; n++)
            {
                LbSpriteDrawResized(scrpos_x * units_per_px / 16, scrpos_y * units_per_px / 16, units_per_px, spr);
                scrpos_x += spr->SWidth;
                spr--;
            }
            scrpos_y += delta_y;
        }
    }
    lbDisplay.DrawFlags = 0;
}

const char* RendererSoftware::GetName() const
{
    return "Software";
}

bool RendererSoftware::SupportsRuntimeSwitch() const
{
    return true;
}

IWorldViewRenderer* RendererSoftware::GetWorldViewRenderer()
{
    return m_worldViewRenderer;
}

IMapFadePass* RendererSoftware::GetMapFadePass()
{
    return m_mapFadePass;
}

ITextRenderer* RendererSoftware::GetTextRenderer()
{
    return m_textRenderer;
}

IUIRenderer* RendererSoftware::GetUIRenderer()
{
    return m_uiRenderer;
}
