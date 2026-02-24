/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CallbackRegistry.cpp
 *     Callback name → function pointer lookup table implementation.
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

#include "CallbackRegistry.h"

#include "../../../bflib_basics.h"
#include "../../../gui_frontbtns.h"
#include "../../../frontend.h"
#include "../../../frontmenu_select.h"

#include "../../../post_inc.h"

#include <string.h>

/* Forward declarations for callbacks defined in other menu modules */
extern "C" void frontend_draw_large_freetext_button(struct GuiButton *gbtn);
extern "C" void frontend_draw_large_dyntext_button(struct GuiButton *gbtn);
extern "C" void frontend_navigate_json_menu(struct GuiButton *gbtn);
extern "C" void frontend_select_campaign(struct GuiButton *gbtn);
extern "C" void frontend_campaign_select_to_hub(struct GuiButton *gbtn);
extern "C" void frontend_start_selected_campaign(struct GuiButton *gbtn);
extern "C" void frontend_draw_campaign_row(struct GuiButton *gbtn);
extern "C" void frontend_hub_high_scores(struct GuiButton *gbtn);
extern "C" void frontend_navigate_to_global_load(struct GuiButton *gbtn);
extern "C" void frontend_navigate_to_campaign_load(struct GuiButton *gbtn);
extern "C" void frontend_draw_global_save_row(struct GuiButton *gbtn);
extern "C" void frontend_global_load_click(struct GuiButton *gbtn);
extern "C" void frontend_global_load_maintain(struct GuiButton *gbtn);
extern "C" void frontend_global_load_up(struct GuiButton *gbtn);
extern "C" void frontend_global_load_up_maintain(struct GuiButton *gbtn);
extern "C" void frontend_global_load_down(struct GuiButton *gbtn);
extern "C" void frontend_global_load_down_maintain(struct GuiButton *gbtn);
extern "C" void frontend_global_load_scroll(struct GuiButton *gbtn);
extern "C" void frontend_draw_global_load_scroll_tab(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select(struct GuiButton *gbtn);
extern "C" void frontend_draw_mappack_select_button(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_up(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_down(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_scroll(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_maintain(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_up_maintain(struct GuiButton *gbtn);
extern "C" void frontend_mappack_select_down_maintain(struct GuiButton *gbtn);
extern "C" void frontend_draw_mappack_scroll_tab(struct GuiButton *gbtn);

const CallbackRegistry::Entry CallbackRegistry::s_table[] = {
    /* Draw callbacks */
    {"frontend_draw_large_menu_button",  frontend_draw_large_menu_button},
    {"frontend_draw_vlarge_menu_button", frontend_draw_vlarge_menu_button},
    {"frontend_draw_small_menu_button",  frontend_draw_small_menu_button},
    {"frontend_draw_scroll_box",         frontend_draw_scroll_box},
    {"frontend_draw_scroll_box_tab",     frontend_draw_scroll_box_tab},
    {"frontend_draw_slider",             frontend_draw_slider},
    {"frontend_draw_small_slider",       frontend_draw_small_slider},
    {"frontend_draw_slider_button",      frontend_draw_slider_button},
    {"frontend_draw_icon",               frontend_draw_icon},
    {"frontend_draw_text",               frontend_draw_text},
    {"frontend_draw_enter_text",         frontend_draw_enter_text},
    {"frontend_draw_computer_players",   frontend_draw_computer_players},
    {"frontend_draw_error_text_box",     frontend_draw_error_text_box},
    {"frontend_draw_product_version",    frontend_draw_product_version},
    {"frontend_draw_large_freetext_button", frontend_draw_large_freetext_button},
    {"frontend_draw_large_dyntext_button",  frontend_draw_large_dyntext_button},
    {"frontend_draw_campaign_select_button", frontend_draw_campaign_select_button},
    {"frontend_draw_campaign_scroll_tab",    frontend_draw_campaign_scroll_tab},
    {"frontend_draw_campaign_row",           frontend_draw_campaign_row},
    {"frontend_draw_global_save_row",        frontend_draw_global_save_row},
    {"frontend_draw_global_load_scroll_tab", frontend_draw_global_load_scroll_tab},
    {"frontend_draw_mappack_select_button",  frontend_draw_mappack_select_button},
    {"frontend_draw_mappack_scroll_tab",     frontend_draw_mappack_scroll_tab},
    /* Hover callbacks */
    {"frontend_over_button",             frontend_over_button},
    /* Click callbacks */
    {"frontend_start_new_game",          frontend_start_new_game},
    {"frontend_load_continue_game",      frontend_load_continue_game},
    {"frontend_change_state",            frontend_change_state},
    {"frontend_ldcampaign_change_state", frontend_ldcampaign_change_state},
    {"frontend_netservice_change_state", frontend_netservice_change_state},
    {"frontend_load_mappacks",           frontend_load_mappacks},
    {"frontend_toggle_computer_players", frontend_toggle_computer_players},
    {"frontend_navigate_json_menu",      frontend_navigate_json_menu},
    {"frontend_select_campaign",         frontend_select_campaign},
    {"frontend_campaign_select_to_hub",  frontend_campaign_select_to_hub},
    {"frontend_start_selected_campaign", frontend_start_selected_campaign},
    {"frontend_hub_high_scores",         frontend_hub_high_scores},
    {"frontend_navigate_to_global_load", frontend_navigate_to_global_load},
    {"frontend_navigate_to_campaign_load", frontend_navigate_to_campaign_load},
    {"frontend_global_load_click",       frontend_global_load_click},
    {"frontend_campaign_select_up",      frontend_campaign_select_up},
    {"frontend_campaign_select_down",    frontend_campaign_select_down},
    {"frontend_campaign_select_scroll",  frontend_campaign_select_scroll},
    {"frontend_mappack_select",          frontend_mappack_select},
    {"frontend_mappack_select_up",       frontend_mappack_select_up},
    {"frontend_mappack_select_down",     frontend_mappack_select_down},
    {"frontend_mappack_select_scroll",   frontend_mappack_select_scroll},
    /* Maintain callbacks */
    {"frontend_continue_game_maintain",          frontend_continue_game_maintain},
    {"frontend_main_menu_load_game_maintain",    frontend_main_menu_load_game_maintain},
    {"frontend_main_menu_netservice_maintain",   frontend_main_menu_netservice_maintain},
    {"frontend_main_menu_highscores_maintain",   frontend_main_menu_highscores_maintain},
    {"frontend_mappacks_maintain",               frontend_mappacks_maintain},
    {"frontend_maintain_error_text_box",         frontend_maintain_error_text_box},
    {"frontend_campaign_select_maintain",         frontend_campaign_select_maintain},
    {"frontend_campaign_select_up_maintain",      frontend_campaign_select_up_maintain},
    {"frontend_campaign_select_down_maintain",    frontend_campaign_select_down_maintain},
    {"frontend_mappack_select_maintain",          frontend_mappack_select_maintain},
    {"frontend_mappack_select_up_maintain",       frontend_mappack_select_up_maintain},
    {"frontend_mappack_select_down_maintain",     frontend_mappack_select_down_maintain},
    {"frontend_global_load_maintain",            frontend_global_load_maintain},
    {"frontend_global_load_up",                  frontend_global_load_up},
    {"frontend_global_load_up_maintain",         frontend_global_load_up_maintain},
    {"frontend_global_load_down",                frontend_global_load_down},
    {"frontend_global_load_down_maintain",       frontend_global_load_down_maintain},
    {"frontend_global_load_scroll",              frontend_global_load_scroll},
    {NULL, NULL}
};

CallbackRegistry& CallbackRegistry::GetInstance()
{
    static CallbackRegistry instance;
    return instance;
}

Gf_Btn_Callback CallbackRegistry::Resolve(const char *name) const
{
    if (name == NULL || name[0] == '\0')
        return NULL;

    for (int i = 0; s_table[i].name != NULL; i++)
    {
        if (strcmp(s_table[i].name, name) == 0)
            return s_table[i].func;
    }
    WARNLOG("Unknown callback: \"%s\"", name);
    return NULL;
}
