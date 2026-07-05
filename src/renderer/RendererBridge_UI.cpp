/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererBridge_UI.cpp
 *     C-callable UIRenderer wrappers (split from RendererManager.cpp).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/RendererManager_Internal.h"
#include "renderer/IUIRenderer.h"
#include "renderer/SpriteHandle.h"
#include "renderer/DrawState.h"     // per-call draw descriptor (lbDisplay draw-state elimination)

#include "bflib_basics.h"
#include "bflib_sprite.h"       // TbSprite
#include "engine_render.h"      // render_fade_tables
#include "bflib_video.h"        // lbDisplay.DrawFlags
#include "gui_draw.h"           // get_panel_sprite, gui_slab, GUI_SLAB_DIMENSION, TiledSprite
#include "config_spritecolors.h" // get_player_colored_icon_idx
#include "custom_sprites.h"     // get_button_sprite_for_player, get_button_sprite
#include "sprites.h"            // GBS_fontchars_number_dig0
#include "player_data.h"        // my_player_number
#include "post_inc.h"

/******************************************************************************/

void UIRenderer_SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SubmitSlabSelector(x1, y1, x2, y2, color, z_depth);
}

void UIRenderer_BeginWorldOverlay(float ndc_z)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetWorldOverlay(ndc_z);
}

void UIRenderer_EndWorldOverlay(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->ClearWorldOverlay();
}

void UIRenderer_BeginWorldOverlayFlat(float ndc_z)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetWorldOverlayFlat(ndc_z);
}

void UIRenderer_EndWorldOverlayFlat(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->ClearWorldOverlayFlat();
}

void UIRenderer_BeginTopOverlay(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetTopOverlay();
}

void UIRenderer_EndTopOverlay(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->ClearTopOverlay();
}

void UIRenderer_BeginZoomBoxOverlay(int x, int y, int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->BeginZoomBoxOverlay(x, y, w, h);
}

void UIRenderer_EndZoomBoxOverlay(int x, int y, int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->EndZoomBoxOverlay(x, y, w, h);
}
 
void UIRenderer_SubmitPanelSprite(int32_t x, int32_t y, int units_per_px, int32_t spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h, false, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitPanelSpriteRaw(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h, false, (unsigned int)lbDisplay.DrawFlags);
}

void RendererSubmitButton(const RendererUIButtonDesc* desc)
{
    if (!desc) return;
    // Background segment strip -> UI node.  Each raw submit reads the ambient
    // lbDisplay.DrawFlags, matching the old inline path where the segments were
    // drawn before the label's flags were set.
    for (int i = 0; i < desc->segment_count && i < RENDERER_BUTTON_MAX_SEGMENTS; ++i) {
        const struct TbSprite* spr = desc->segments[i].spr;
        if (spr)
            UIRenderer_SubmitPanelSpriteRaw(desc->segments[i].x, desc->segments[i].y,
                                            desc->units_per_px, spr);
    }
    // Label -> text node (not the LbText* globals).  The text path reads
    // lbDisplay.DrawFlags, so set it here exactly as the old code did before
    // LbTextDrawResized.
    if (desc->text && desc->font) {
        lbDisplay.DrawFlags = desc->label_draw_flags;
        TextRenderer_SetFont(desc->font);
        TextRenderer_SetWindow(desc->text_x, desc->text_y, desc->text_w, desc->text_h);
        TextRenderer_DrawTextResized(0, 0, desc->units_per_px, desc->text);
    }
}

void UIRenderer_SubmitPanelSpriteWithBg(int32_t x, int32_t y, int units_per_px,
                                        const struct TbSprite* spr, unsigned char bg_color_idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !spr) return;
    // Compute the sprite's on-screen dimensions.
    int32_t w = ((int32_t)spr->SWidth  * units_per_px + 8) / 16;
    int32_t h = ((int32_t)spr->SHeight * units_per_px + 8) / 16;
    // Submit opaque background fill, then the sprite on top.
    ui->SubmitSolidBox(x, y, w, h, bg_color_idx);
    SpriteHandle sh = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, sh, false, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitPanelSpriteRawColored(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr, unsigned char color_idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSpriteColored(x, y, units_per_px, h, (uint8_t)color_idx, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitOutlineBox(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx)
{
    if (w < 2 || h < 2) return;
    // Decompose outline into four 1-pixel-thick border strips (top/bottom/left/right).
    UIRenderer_SubmitSolidBox(x,       y,       w, 1, color_idx);  // top
    UIRenderer_SubmitSolidBox(x,       y + h - 1, w, 1, color_idx);  // bottom
    UIRenderer_SubmitSolidBox(x,       y,       1, h, color_idx);  // left
    UIRenderer_SubmitSolidBox(x + w - 1, y,     1, h, color_idx);  // right
}

void UIRenderer_SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr, int remap_row)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSpriteRemap(x, y, units_per_px, h, remap_row, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitPanelSpriteCentered(int32_t x, int32_t y, int units_per_px, int32_t spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    if (!spr) return;
    int32_t ox = ((int32_t)spr->SWidth  * units_per_px + 8) / 16 / 2;
    int32_t oy = ((int32_t)spr->SHeight * units_per_px + 8) / 16 / 2;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x - ox, y - oy, units_per_px, h, false, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitButtonSprite(int32_t x, int32_t y, int units_per_px, short spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h, false, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitButtonSpriteFlipped(int32_t x, int32_t y, int units_per_px, short spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h, true, (unsigned int)lbDisplay.DrawFlags);
}

void UIRenderer_SubmitDigitSprites(int32_t center_x, int32_t y, int32_t w, int32_t h, int64_t value)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || value <= 0) return;

    // Count digits
    int ndigits = 0;
    for (int64_t v = value; v > 0; v /= 10)
        ndigits++;

    // Draw right-to-left, centered on center_x
    int32_t pos_x = center_x + w * (ndigits - 1) / 2;
    for (int64_t v = value; v > 0; v /= 10)
    {
        const struct TbSprite* spr = get_button_sprite((short)((v % 10) + GBS_fontchars_number_dig0));
        SpriteHandle hspr = RendererResolveSprite(spr);
        ui->SubmitScaledSprite(pos_x, y, w, h, hspr, (unsigned int)lbDisplay.DrawFlags);
        pos_x -= w;
    }
}

void UIRenderer_SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h, const struct TbSprite *spr)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) {
        SpriteHandle hspr = RendererResolveSprite(spr);
        ui->SubmitScaledSprite(x, y, w, h, hspr, (unsigned int)lbDisplay.DrawFlags);
    }
}

void UIRenderer_SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SubmitSolidBox(x, y, w, h, color_idx);
}

void UIRenderer_SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, unsigned char color_idx, float alpha)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SubmitSolidBoxAlpha(x, y, w, h, color_idx, alpha);
}

void UIRenderer_SubmitCircle(int32_t x, int32_t y, int32_t radius, unsigned char color_idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    if (RendererHasGPURenderPath())
    {
        int32_t d = radius * 2 + 1;
        ui->SubmitSolidBox(x - radius, y - radius, d, d, color_idx);
        return;
    }
    ui->SubmitCircle(x, y, radius, color_idx);
}

void UIRenderer_SetSlabTexture(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    SYNCLOG("UIRenderer_SetSlabTexture: ui=%p gui_slab=%p", (void*)ui, (void*)gui_slab);
    if (ui && gui_slab)
        ui->UpdateSlabTexture(gui_slab, GUI_SLAB_DIMENSION);
}

TbBool UIRenderer_SubmitSlabBackground(int x, int y, int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return 0;
    return ui->SubmitSlabBackground(x, y, w, h) ? 1 : 0;
}

unsigned char* UIRenderer_AcquireMinimapBuffer(int size)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) return ui->AcquireMinimapBuffer(size);
    return nullptr;
}

void UIRenderer_SubmitMinimap(int screen_x, int screen_y, int size)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SubmitMinimap(screen_x, screen_y, size);
}

void UIRenderer_SetupMinimapBackground(int diaglen, int panel_x, int panel_y)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetupMinimapBackground(diaglen, panel_x, panel_y);
}

TbBool UIRenderer_GetMinimapOpaqueBlackIndex(unsigned char* idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !idx) return 0;
    uint8_t color_idx = 0;
    if (!ui->GetMinimapOpaqueBlackIndex(&color_idx)) return 0;
    *idx = color_idx;
    return 1;
}
 
void UIRenderer_SubmitTiledSprite(int32_t x, int32_t y, int units_per_px, const struct TiledSprite* bigspr)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !bigspr) return;
    int32_t cur_y = y;
    for (int sy = 0; sy < bigspr->y_num; sy++)
    {
        int32_t cur_x = x;
        int32_t delta_y = 0;
        unsigned short spr_idx = bigspr->spr_idx[sy][0];
        for (int sx = 0; sx < bigspr->x_num; sx++)
        {
            const struct TbSprite* spr = get_panel_sprite(spr_idx);
            if (!spr) { spr_idx++; continue; }
            int32_t delta_x = (int32_t)spr->SWidth * units_per_px / 16;
            delta_y      = (int32_t)spr->SHeight * units_per_px / 16;
            if (spr_idx)
            {
                SpriteHandle h = RendererResolveSprite(spr);
                ui->SubmitScaledSprite(cur_x, cur_y, delta_x, delta_y, h, (unsigned int)lbDisplay.DrawFlags);
            }
            spr_idx++;
            cur_x += delta_x;
        }
        cur_y += delta_y;
    }
}

void UIRenderer_SetGameViewport(int x, int y, int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetGameViewport(x, y, w, h);
}

void UIRenderer_DrawGameUI(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawGameUI();
}

void UIRenderer_DrawWorldSpriteLayerRT(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawWorldSpriteLayerRT();
}

void UIRenderer_DrawWorldOverlayFlatLayerRT(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawWorldOverlayFlatLayerRT();
}

void UIRenderer_DrawFront(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawFront();
}

void UIRenderer_Draw(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->Draw();
}

void UIRenderer_Clear(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->Clear();
}

unsigned char* UIRenderer_QueryPanelSpriteMask(int32_t spridx, int* out_w, int* out_h, int* out_stride)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return nullptr;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    SpriteHandle h = RendererResolveSprite(spr);
    if (h == kInvalidSpriteHandle) return nullptr;
    return (unsigned char*)ui->QuerySpriteMask(h, out_w, out_h, out_stride);
}

// ── Raw sprite submission (used by LbSpriteDraw intercept path) ───────────────
// These wrappers accept raw TbSprite* directly (no sprite-index lookup) and
// resolve via RendererResolveSprite.  They route through the same IUIRenderer
// Submit* API as the panel-sprite wrappers so the IR / double-buffer machinery
// handles them transparently.
//
// When m_ui_write_cmds is set (game thread), submissions go to the IR write
// buffer.  When it is null (render thread world-pass), submissions go directly
// to m_quads[layer] for the current draw call.

TbResult UIRenderer_SubmitRawSprite(long x, long y, const struct TbSprite* spr,
                                     unsigned int draw_flags)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !spr) return Lb_FAIL;
    if (!RendererHasGPURenderPath())
        return ui->SubmitRawSprite(x, y, spr, draw_flags);
    SpriteHandle h = RendererResolveSprite(spr);
    if (h == kInvalidSpriteHandle) return Lb_FAIL;  // atlas miss → caller falls through to SW
    ui->SubmitPanelSprite((int32_t)x, (int32_t)y, 16, h, false, draw_flags);
    return Lb_OK;
}

TbResult UIRenderer_SubmitRawSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                              unsigned char colour,
                                              unsigned int draw_flags)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !spr) return Lb_FAIL;
    if (!RendererHasGPURenderPath())
        return ui->SubmitRawSpriteOneColour(x, y, spr, colour, draw_flags);
    SpriteHandle h = RendererResolveSprite(spr);
    if (h == kInvalidSpriteHandle) return Lb_FAIL;
    ui->SubmitPanelSpriteColored((int32_t)x, (int32_t)y, 16, h, colour, draw_flags);
    return Lb_OK;
}

TbResult UIRenderer_SubmitRawSpriteRemap(long x, long y, const struct TbSprite* spr,
                                          const unsigned char* cmap,
                                          unsigned int draw_flags)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui || !spr || !cmap) return Lb_FAIL;
    if (!RendererHasGPURenderPath())
        return ui->SubmitRawSpriteRemap(x, y, spr, cmap, draw_flags);
    SpriteHandle h = RendererResolveSprite(spr);
    if (h == kInvalidSpriteHandle) return Lb_FAIL;
    // Compute the remap row from the pointer offset into render_fade_tables.
    // All creature/player colour remaps use rows within that table.
    int remap_row = 0;
    if (render_fade_tables && cmap >= render_fade_tables)
        remap_row = (int)((cmap - render_fade_tables) / 256);
    ui->SubmitPanelSpriteRemap((int32_t)x, (int32_t)y, 16, h, remap_row, draw_flags);
    return Lb_OK;
}

void UIRenderer_DrawWorldSprites(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawWorldSprites();
}

void UIRenderer_SetScreenSize(int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetScreenSize(w, h);
}
