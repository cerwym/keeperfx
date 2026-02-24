/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FreeformText.cpp
 *     Freeform text slot management and draw callback implementation.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../../pre_inc.h"

#include "FreeformText.h"

#include "../../../bflib_basics.h"
#include "../../../bflib_sprfnt.h"
#include "../../../bflib_vidraw.h"
#include "../../../bflib_video.h"
#include "../../../bflib_sprite.h"
#include "../../../bflib_datetm.h"
#include "../../../frontend.h"
#include "../../../gui_draw.h"
#include "../../../custom_sprites.h"
#include "../../../sprites.h"
#include "../../../vidmode.h"

#include "../../../post_inc.h"

#include <string.h>

FreeformText& FreeformText::GetInstance()
{
    static FreeformText instance;
    return instance;
}

long FreeformText::Register(const char *text, unsigned char fontIndex)
{
    if (m_count >= MENU_MAX_FREETEXT)
    {
        ERRORLOG("Freeform text slots exhausted (max %d)", MENU_MAX_FREETEXT);
        return -1;
    }
    int slot = m_count++;
    strncpy(m_texts[slot], text, MENU_MAX_FREETEXT_LEN - 1);
    m_texts[slot][MENU_MAX_FREETEXT_LEN - 1] = '\0';
    m_fonts[slot] = fontIndex;
    return MENU_FREETEXT_CONTENT_BASE + slot;
}

const char* FreeformText::GetText(long contentLval) const
{
    long slot = contentLval - MENU_FREETEXT_CONTENT_BASE;
    if (slot < 0 || slot >= m_count)
        return NULL;
    return m_texts[slot];
}

unsigned char FreeformText::GetFont(long contentLval) const
{
    long slot = contentLval - MENU_FREETEXT_CONTENT_BASE;
    if (slot < 0 || slot >= m_count)
        return 1;
    return m_fonts[slot];
}

void FreeformText::DrawButton(struct GuiButton *gbtn) const
{
    static const long large_button_sprite_anims[] = {
        GFS_hugebutton_a01l,
        GFS_hugebutton_a02l,
        GFS_hugebutton_a03l,
        GFS_hugebutton_a04l,
        GFS_hugebutton_a05l,
        GFS_hugebutton_a04l,
        GFS_hugebutton_a03l,
        GFS_hugebutton_a02l,
    };

    const char *text = GetText(gbtn->content.lval);
    if (text == NULL)
        text = "";

    unsigned long febtn_idx = gbtn->content.lval;
    unsigned long spridx;
    int fntidx;

    if ((gbtn->flags & LbBtnF_Enabled) == 0)
    {
        fntidx = 3;
        spridx = GFS_hugebutton_a05l;
    }
    else
    {
        fntidx = GetFont(gbtn->content.lval);
        if ((febtn_idx > 0) && (frontend_mouse_over_button == (long)febtn_idx))
            spridx = large_button_sprite_anims[((LbTimerClock() - frontend_mouse_over_button_start_time) / 100) & 7];
        else
            spridx = GFS_hugebutton_a05l;
    }

    const struct TbSprite *spr;
    int units_per_px = simple_frontend_sprite_height_units_per_px(gbtn, GFS_hugebutton_a05l, 100);
    long x = gbtn->scr_pos_x;
    long y = gbtn->scr_pos_y;

    spr = get_frontend_sprite(spridx);
    LbSpriteDrawResized(x, y, units_per_px, spr);
    x += spr->SWidth * units_per_px / 16;
    spr = get_frontend_sprite(spridx + 1);
    LbSpriteDrawResized(x, y, units_per_px, spr);
    x += spr->SWidth * units_per_px / 16;
    spr = get_frontend_sprite(spridx + 2);
    LbSpriteDrawResized(x, y, units_per_px, spr);

    if (text[0] != '\0')
    {
        lbDisplay.DrawFlags = Lb_TEXT_HALIGN_CENTER;
        LbTextSetFont(frontend_font[fntidx]);
        spr = get_frontend_sprite(spridx);
        int h = LbTextHeight(text) * units_per_px / 16;
        x = gbtn->scr_pos_x + ((40 * units_per_px / 16) >> 1);
        y = gbtn->scr_pos_y + ((spr->SHeight * units_per_px / 16 - h) >> 1);
        LbTextSetWindow(x, y, gbtn->width - 40 * units_per_px / 16, h);
        LbTextDrawResized(0, 0, units_per_px, text);
    }
}

void FreeformText::Reset()
{
    m_count = 0;
}
