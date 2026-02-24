/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuLoader.cpp
 *     Thin extern "C" wrappers delegating to C++ class implementations.
 * @par Purpose:
 *     Provides the stable C-linkage API that the frontend engine calls,
 *     routing to CallbackRegistry, FreeformText, JsonParser, MenuBuilder,
 *     and MenuRegistry singletons.
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

#include "MenuLoader.h"
#include "CallbackRegistry.h"
#include "FreeformText.h"
#include "DynamicText.h"
#include "JsonParser.h"
#include "MenuBuilder.h"
#include "MenuRegistry.h"

#include "../../../config_campaigns.h"
#include "../../../frontend.h"
#include "../../../front_highscore.h"
#include "../../../frontmenu_select.h"
#include "../../../custom_sprites.h"
#include "../../../bflib_basics.h"
#include "../../../bflib_sprite.h"
#include "../../../bflib_sprfnt.h"
#include "../../../bflib_video.h"
#include "../../../bflib_vidraw.h"
#include "../../../kjm_input.h"
#include "../../../game_legacy.h"
#include "../../../sprites.h"
#include "../../../game_saves.h"
#include "../../../keeperfx.hpp"
#include "../../../bflib_datetm.h"

#include "../../../post_inc.h"

#include <time.h>

/******************************************************************************/
/* extern "C" API — thin wrappers                                             */
/******************************************************************************/

Gf_Btn_Callback resolve_button_callback(const char *name)
{
    return CallbackRegistry::GetInstance().Resolve(name);
}

TbBool load_menu_from_json(const char *filepath, struct MenuDefinition *menu_def)
{
    return JsonParser::GetInstance().LoadMenuFromJson(filepath, menu_def);
}

struct GuiButtonInit *build_button_init_array(const struct MenuDefinition *menu_def)
{
    return MenuBuilder::BuildButtonInitArray(menu_def);
}

void build_gui_menu(const struct MenuDefinition *menu_def, struct GuiButtonInit *buttons, struct GuiMenu *menu_out)
{
    MenuBuilder::BuildGuiMenu(menu_def, buttons, menu_out);
}

void free_button_init_array(struct GuiButtonInit *buttons)
{
    MenuBuilder::FreeArray(buttons);
}

void init_json_menus(void)
{
    MenuRegistry::GetInstance().Init();
}

void shutdown_json_menus(void)
{
    MenuRegistry::GetInstance().Shutdown();
}

void frontend_navigate_json_menu(struct GuiButton *gbtn)
{
    MenuRegistry::GetInstance().NavigateTo((int)gbtn->btype_value);
}

void json_menu_setup(void)
{
    MenuRegistry::GetInstance().SetupActiveMenu();
}

void json_menu_shutdown(void)
{
    MenuRegistry::GetInstance().ShutdownActiveMenu();
}

void frontend_draw_large_freetext_button(struct GuiButton *gbtn)
{
    FreeformText::GetInstance().DrawButton(gbtn);
}

void frontend_draw_large_dyntext_button(struct GuiButton *gbtn)
{
    DynamicText::GetInstance().DrawButton(gbtn);
}

const char *get_freeform_text(long content_lval)
{
    return FreeformText::GetInstance().GetText(content_lval);
}

void frontend_select_campaign(struct GuiButton *gbtn)
{
    int idx = (int)gbtn->btype_value;
    if (idx < 0 || idx >= (int)campaigns_list.items_num)
    {
        ERRORLOG("Campaign index %d out of range (max %lu)", idx, campaigns_list.items_num);
        return;
    }
    const char *fname = campaigns_list.items[idx].fname;
    JUSTLOG("Selecting campaign %d: \"%s\" (%s)", idx, campaigns_list.items[idx].display_name, fname);
    change_campaign(fname);
    continue_game_option_available = continue_game_available();
    initialise_load_game_slots();
    MenuRegistry::GetInstance().NavigateToByName("campaign_hub");
}

void frontend_campaign_select_to_hub(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long btn_idx = gbtn->content.lval;
    long i = select_campaign_scroll_offset + btn_idx - 45;
    if (i < 0 || i >= (long)campaigns_list.items_num)
        return;
    struct GameCampaign *campgn = &campaigns_list.items[i];
    JUSTLOG("Selecting campaign %ld: \"%s\" (%s)", i, campgn->display_name, campgn->fname);
    change_campaign(campgn->fname);
    continue_game_option_available = continue_game_available();
    initialise_load_game_slots();
    MenuRegistry::GetInstance().NavigateToByName("campaign_hub");
}

void frontend_start_selected_campaign(struct GuiButton *gbtn)
{
    if (campaign.fname[0] == '\0')
    {
        ERRORLOG("No campaign selected");
        return;
    }
    if (!frontend_start_new_campaign(campaign.fname))
    {
        ERRORLOG("Unable to start campaign \"%s\"", campaign.fname);
        return;
    }
    frontend_set_state(FeSt_CAMPAIGN_INTRO);
}

void navigate_to_json_menu_by_name(const char *menuId)
{
    MenuRegistry::GetInstance().NavigateToByName(menuId);
}

void frontend_hub_high_scores(struct GuiButton *gbtn)
{
    fe_high_score_table_from_main_menu = 0;
    fe_high_score_table_from_json_menu = 1;
    frontend_set_state(FeSt_HIGH_SCORES);
}

void frontend_draw_campaign_row(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long btn_idx = gbtn->content.lval;
    long i = select_campaign_scroll_offset + btn_idx - 45;
    struct GameCampaign *campgn = NULL;
    if ((i >= 0) && (i < (long)campaigns_list.items_num))
        campgn = &campaigns_list.items[i];
    if (campgn == NULL)
        return;
    int font_idx;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
        font_idx = 2;
    else
        font_idx = 1;
    LbTextSetFont(frontend_font[font_idx]);
    int tx_units_per_px = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    int line_h = LbTextLineHeight() * tx_units_per_px / 16;
    // Draw campaign name left-aligned
    lbDisplay.DrawFlags = Lb_TEXT_HALIGN_LEFT;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, gbtn->width, line_h);
    LbTextDrawResized(0, 0, tx_units_per_px, campgn->display_name);
    // Draw crown icon + level count right-aligned
    const struct TbSprite *crown = get_ui_sprite("button", GBS_guisymbols_sym_crown);
    if (crown != NULL && crown->SWidth > 0)
    {
        int icon_w = (crown->SWidth * tx_units_per_px + 8) / 16;
        int icon_h = (crown->SHeight * tx_units_per_px + 8) / 16;
        int icon_y = gbtn->scr_pos_y + (line_h - icon_h) / 2;
        char level_text[16];
        snprintf(level_text, sizeof(level_text), "%lu", campgn->single_levels_count);
        int text_w = LbTextStringWidth(level_text) * tx_units_per_px / 16;
        int total_w = icon_w + 4 + text_w;
        int right_x = gbtn->scr_pos_x + gbtn->width - total_w;
        LbSpriteDrawResized(right_x, icon_y, tx_units_per_px, crown);
        lbDisplay.DrawFlags = Lb_TEXT_HALIGN_LEFT;
        LbTextSetWindow(right_x + icon_w + 4, gbtn->scr_pos_y, text_w + 4, line_h);
        LbTextDrawResized(0, 0, tx_units_per_px, level_text);
    }
}

/******************************************************************************/
/* Global Load Game Browser Callbacks                                         */
/******************************************************************************/

static void format_save_date(long mtime, char *buf, int buflen)
{
    if (mtime <= 0) {
        snprintf(buf, buflen, "---");
        return;
    }
    time_t t = (time_t)mtime;
    struct tm *tm = localtime(&t);
    if (tm) {
        strftime(buf, buflen, "%d %b %Y %H:%M", tm);
    } else {
        snprintf(buf, buflen, "---");
    }
}

void frontend_navigate_to_global_load(struct GuiButton *gbtn)
{
    global_load_is_all_campaigns = true;
    scan_all_campaign_saves();
    MenuRegistry::GetInstance().NavigateToByName("global_load");
}

void frontend_navigate_to_campaign_load(struct GuiButton *gbtn)
{
    global_load_is_all_campaigns = false;
    scan_current_campaign_saves();
    MenuRegistry::GetInstance().NavigateToByName("global_load");
}

void frontend_draw_global_save_row(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long btn_idx = gbtn->content.lval;
    long i = global_load_scroll_offset + btn_idx - 45;
    if (i < 0 || i >= global_save_count)
        return;
    struct GlobalSaveEntry *entry = &global_save_entries[i];
    if (!entry->in_use)
        return;

    int font_idx;
    if ((btn_idx > 0) && (frontend_mouse_over_button == btn_idx))
        font_idx = 2;
    else
        font_idx = 1;

    // Measure date text first to reserve space on the right
    LbTextSetFont(frontend_font[2]);
    int date_tx = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    char date_text[48];
    format_save_date(entry->modified_time, date_text, sizeof(date_text));
    int date_w = LbTextStringWidth(date_text) * date_tx / 16;
    int date_margin = 12;

    // Draw save name left-aligned, clipped to avoid date area
    LbTextSetFont(frontend_font[font_idx]);
    int tx_units_per_px = (gbtn->height * 13 / 11) * 16 / LbTextLineHeight();
    int line_h = LbTextLineHeight() * tx_units_per_px / 16;

    char display_text[128];
    if (global_load_is_all_campaigns)
        snprintf(display_text, sizeof(display_text), "%s - %s", entry->campaign_name, entry->save_textname);
    else
        snprintf(display_text, sizeof(display_text), "%s", entry->save_textname);

    int name_avail_w = gbtn->width - date_w - date_margin;
    int name_text_w = LbTextStringWidth(display_text) * tx_units_per_px / 16;
    int text_x_offset = 0;

    // Scroll text horizontally when hovered and it overflows
    if (name_text_w > name_avail_w && (frontend_mouse_over_button == btn_idx))
    {
        int overflow = name_text_w - name_avail_w;
        int cycle_ms = overflow * 12 + 2000; // slower for longer text, plus pause
        int t = (int)(LbTimerClock() % cycle_ms);
        int scroll_time = overflow * 12;
        if (t < 1000) {
            text_x_offset = 0; // pause at start
        } else if (t < 1000 + scroll_time) {
            text_x_offset = -((t - 1000) * overflow / scroll_time);
        } else {
            text_x_offset = -overflow; // pause at end
        }
    }

    lbDisplay.DrawFlags = Lb_TEXT_HALIGN_LEFT;
    LbTextSetWindow(gbtn->scr_pos_x, gbtn->scr_pos_y, name_avail_w, line_h);
    LbTextDrawResized(text_x_offset, 0, tx_units_per_px, display_text);

    // Draw date right-aligned in smaller font
    LbTextSetFont(frontend_font[2]);
    int date_h = LbTextLineHeight() * date_tx / 16;
    int date_x = gbtn->scr_pos_x + gbtn->width - date_w - 4;
    LbTextSetWindow(date_x, gbtn->scr_pos_y, date_w + 8, date_h);
    lbDisplay.DrawFlags = Lb_TEXT_HALIGN_LEFT;
    LbTextDrawResized(0, 0, date_tx, date_text);
}

void frontend_global_load_click(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long btn_idx = gbtn->content.lval;
    long i = global_load_scroll_offset + btn_idx - 45;
    if (i < 0 || i >= global_save_count)
        return;
    struct GlobalSaveEntry *entry = &global_save_entries[i];
    if (!entry->in_use)
        return;

    // Switch to the save's campaign if needed
    if (strcmp(campaign.fname, entry->campaign_fname) != 0)
    {
        if (!change_campaign(entry->campaign_fname))
        {
            ERRORLOG("Failed to change campaign to \"%s\"", entry->campaign_fname);
            return;
        }
    }

    game.save_game_slot = entry->slot_num;
    if (is_save_game_loadable(entry->slot_num))
    {
        frontend_set_state(FeSt_LOAD_GAME);
    }
}

void frontend_global_load_maintain(struct GuiButton *gbtn)
{
    if (gbtn == NULL)
        return;
    long btn_idx = gbtn->content.lval;
    long i = global_load_scroll_offset + btn_idx - 45;
    if (i >= 0 && i < global_save_count && global_save_entries[i].in_use)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags = (gbtn->flags & ~LbBtnF_Enabled);
}

void frontend_global_load_up(struct GuiButton *gbtn)
{
    if (global_load_scroll_offset > 0)
        global_load_scroll_offset--;
}

void frontend_global_load_up_maintain(struct GuiButton *gbtn)
{
    if (wheel_scrolled_up && global_load_scroll_offset > 0)
        global_load_scroll_offset--;
    if (wheel_scrolled_down && global_load_scroll_offset < global_save_count - 7)
        global_load_scroll_offset++;
    if (global_load_scroll_offset > 0)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags = (gbtn->flags & ~LbBtnF_Enabled);
}

void frontend_global_load_down(struct GuiButton *gbtn)
{
    if (global_load_scroll_offset < global_save_count - 7)
        global_load_scroll_offset++;
}

void frontend_global_load_down_maintain(struct GuiButton *gbtn)
{
    if (global_load_scroll_offset < global_save_count - 7)
        gbtn->flags |= LbBtnF_Enabled;
    else
        gbtn->flags = (gbtn->flags & ~LbBtnF_Enabled);
}

void frontend_global_load_scroll(struct GuiButton *gbtn)
{
    global_load_scroll_offset = frontend_scroll_tab_to_offset(gbtn,
        GetMouseY(), 5, global_save_count);
}

void frontend_draw_global_load_scroll_tab(struct GuiButton *gbtn)
{
    frontend_draw_scroll_tab(gbtn, global_load_scroll_offset,
        5, global_save_count);
}
