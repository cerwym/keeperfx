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

#include "bflib_basics.h"
#include "bflib_sprite.h"       // TbSprite
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

void UIRenderer_BeginWorldDepth(float ndc_z)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetWorldDepth(ndc_z);
}

void UIRenderer_EndWorldDepth(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->ClearWorldDepth();
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

void UIRenderer_SubmitPanelSprite(int32_t x, int32_t y, int units_per_px, int32_t spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_panel_sprite(get_player_colored_icon_idx(spridx, my_player_number));
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h);
}

void UIRenderer_SubmitPanelSpriteRaw(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h);
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
    ui->SubmitPanelSprite(x, y, units_per_px, sh);
}

void UIRenderer_SubmitPanelSpriteRawColored(int32_t x, int32_t y, int units_per_px, const struct TbSprite* spr, unsigned char color_idx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSpriteColored(x, y, units_per_px, h, (uint8_t)color_idx);
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
    ui->SubmitPanelSpriteRemap(x, y, units_per_px, h, remap_row);
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
    ui->SubmitPanelSprite(x - ox, y - oy, units_per_px, h);
}

void UIRenderer_SubmitButtonSprite(int32_t x, int32_t y, int units_per_px, short spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h);
}

void UIRenderer_SubmitButtonSpriteFlipped(int32_t x, int32_t y, int units_per_px, short spridx)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (!ui) return;
    const struct TbSprite* spr = get_button_sprite_for_player(spridx, my_player_number);
    SpriteHandle h = RendererResolveSprite(spr);
    ui->SubmitPanelSprite(x, y, units_per_px, h, true);
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
        ui->SubmitScaledSprite(pos_x, y, w, h, hspr);
        pos_x -= w;
    }
}

void UIRenderer_SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h, const struct TbSprite *spr)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) {
        SpriteHandle hspr = RendererResolveSprite(spr);
        ui->SubmitScaledSprite(x, y, w, h, hspr);
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
                ui->SubmitScaledSprite(cur_x, cur_y, delta_x, delta_y, h);
            }
            spr_idx++;
            cur_x += delta_x;
        }
        cur_y += delta_y;
    }
}

void UIRenderer_SetLayer(int layer)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetLayer(layer);
}

void UIRenderer_SetGameViewport(int x, int y, int w, int h)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->SetGameViewport(x, y, w, h);
}

void UIRenderer_DrawBack(void)
{
    IUIRenderer* ui = RendererGetUIRenderer();
    if (ui) ui->DrawBack();
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
