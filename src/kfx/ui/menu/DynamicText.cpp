/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DynamicText.cpp
 *     Dynamic text provider implementation.
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

#include "DynamicText.h"

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
#include "../../../config_campaigns.h"
#include "../../../keeperfx/achievement/achievement_api.h"

#include "../../../post_inc.h"

#include <string.h>

/******************************************************************************/
/* Built-in text providers                                                    */
/******************************************************************************/

static const char* provider_active_campaign_name(void)
{
    if (campaign.display_name[0] != '\0')
        return campaign.display_name;
    if (campaign.name[0] != '\0')
        return campaign.name;
    return "No Campaign";
}

static const char* provider_active_campaign_fname(void)
{
    if (campaign.fname[0] != '\0')
        return campaign.fname;
    return "";
}

static const char* provider_achievement_progress_text(void)
{
    static char buf[64];
    int total = achievements_get_count();
    if (total <= 0)
        return "No Achievements";
    int unlocked = achievements_get_unlocked_count();
    snprintf(buf, sizeof(buf), "Achievements %d/%d", unlocked, total);
    return buf;
}

/******************************************************************************/
/* Provider registry                                                          */
/******************************************************************************/

const DynamicText::ProviderEntry DynamicText::s_providers[] = {
    {"active_campaign_name",    provider_active_campaign_name},
    {"active_campaign_fname",   provider_active_campaign_fname},
    {"achievement_progress_text", provider_achievement_progress_text},
    {NULL, NULL}
};

DynTextProvider DynamicText::ResolveProvider(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return NULL;

    for (int i = 0; s_providers[i].name != NULL; i++)
    {
        if (strcmp(s_providers[i].name, name) == 0)
            return s_providers[i].func;
    }
    WARNLOG("Unknown dynamic text provider: \"%s\"", name);
    return NULL;
}

/******************************************************************************/
/* DynamicText implementation                                                 */
/******************************************************************************/

DynamicText& DynamicText::GetInstance()
{
    static DynamicText instance;
    return instance;
}

long DynamicText::Register(const char *providerName, unsigned char fontIndex)
{
    DynTextProvider func = ResolveProvider(providerName);
    if (func == NULL)
        return -1;

    if (m_count >= MENU_MAX_DYNTEXT)
    {
        ERRORLOG("Dynamic text slots exhausted (max %d)", MENU_MAX_DYNTEXT);
        return -1;
    }

    int slot = m_count++;
    m_slots[slot] = func;
    m_fonts[slot] = fontIndex;
    return MENU_DYNTEXT_CONTENT_BASE + slot;
}

const char* DynamicText::GetText(long contentLval) const
{
    long slot = contentLval - MENU_DYNTEXT_CONTENT_BASE;
    if (slot < 0 || slot >= m_count || m_slots[slot] == NULL)
        return NULL;
    return m_slots[slot]();
}

unsigned char DynamicText::GetFont(long contentLval) const
{
    long slot = contentLval - MENU_DYNTEXT_CONTENT_BASE;
    if (slot < 0 || slot >= m_count)
        return 1;
    return m_fonts[slot];
}

void DynamicText::DrawButton(struct GuiButton *gbtn) const
{
    // TODO : Allow for buttons loaded in other banks
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

void DynamicText::Reset()
{
    m_count = 0;
}
