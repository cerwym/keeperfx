/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file JsonParser.cpp
 *     JSON menu file parsing implementation using CentiJSON.
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

#include "JsonParser.h"

#include <json.h>
#include <json-dom.h>

#include "../../../bflib_fileio.h"
#include "../../../config_strings.h"
#include "../../../gui_draw.h"
#include "../../../vidmode.h"
#include "../../../frontend.h"

#include "../../../post_inc.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/******************************************************************************/
/* Content name lookup table                                                  */
/******************************************************************************/

const JsonParser::ContentNameEntry JsonParser::s_contentNames[] = {
    {"main_menu",              1},
    {"start_new_game",         2},
    {"load_game",              3},
    {"multiplayer",            4},
    {"quit",                   5},
    {"return_to_main",         6},
    {"load_game_title",        7},
    {"continue_game",          8},
    {"play_intro",             9},
    {"net_service_menu",      10},
    {"net_session_menu",      11},
    {"game_menu",             12},
    {"net_join_game",         13},
    {"net_create_game",       14},
    {"net_start_game",        15},
    {"cancel",                16},
    {"net_name",              19},
    {"level",                 22},
    {"net_sessions_header",   29},
    {"games_header",          30},
    {"players_header",        31},
    {"levels_header",         32},
    {"net_services_header",   33},
    {"net_messages_header",   34},
    {"net_modem_menu",        53},
    {"net_serial_menu",       54},
    {"net_com_port_header",   55},
    {"net_speed_header",      56},
    {"net_irq",               61},
    {"net_init",              66},
    {"net_hangup",            67},
    {"net_dial",              68},
    {"net_answer",            69},
    {"net_phone_number",      71},
    {"net_continue",          72},
    {"credits",               82},
    {"ok",                    83},
    {"statistics",            84},
    {"high_score_table",      85},
    {"team_choose_game",      86},
    {"team_game_type_header", 87},
    {"net_start",             88},
    {"define_keys_title",     92},
    {"define_keys",           95},
    {"options_title",         96},
    {"options",               97},
    {"return_to_options",     98},
    {"sound_options",         99},
    {"mouse_options",        100},
    {"sensitivity",          101},
    {"invert_mouse",         102},
    {"computer_assist",      103},
    {"high_scores",          104},
    {"free_play_levels",     106},
    {"free_play_title",      107},
    {"land_selection_title", 108},
    {"campaigns_header",     109},
    {"add_computer",         110},
    {"return_to_freeplay",   111},
    {"map_packs_header",     112},
    {"levelpack_title",      107},
    {"levelpacks_header",    112},
    {NULL, 0}
};

/******************************************************************************/
/* JSON helpers                                                               */
/******************************************************************************/

static const char *json_get_string(VALUE *obj, const char *key)
{
    VALUE *v = value_dict_get(obj, key);
    if (v != NULL && value_type(v) == VALUE_STRING)
        return value_string(v);
    return NULL;
}

static int json_get_int(VALUE *obj, const char *key, int default_val)
{
    VALUE *v = value_dict_get(obj, key);
    if (v != NULL && value_is_compatible(v, VALUE_INT32))
        return value_int32(v);
    return default_val;
}

static TbBool json_get_bool(VALUE *obj, const char *key, TbBool default_val)
{
    VALUE *v = value_dict_get(obj, key);
    if (v != NULL && value_type(v) == VALUE_BOOL)
        return value_bool(v) ? true : false;
    return default_val;
}

/******************************************************************************/
/* Parsing helpers                                                            */
/******************************************************************************/

static enum SpriteComposition parse_composition(const char *str)
{
    if (str == NULL)                         return SprComp_None;
    if (strcmp(str, "single") == 0)           return SprComp_Single;
    if (strcmp(str, "three_piece") == 0)      return SprComp_ThreePiece;
    if (strcmp(str, "four_piece") == 0)       return SprComp_FourPiece;
    if (strcmp(str, "nine_slice") == 0)       return SprComp_NineSlice;
    WARNLOG("Unknown composition type: \"%s\"", str);
    return SprComp_None;
}

static TbBool parse_sprite_frame(VALUE *frame_val, struct SpriteFrame *frame)
{
    memset(frame, 0, sizeof(*frame));
    if (value_type(frame_val) == VALUE_ARRAY)
    {
        frame->sprite_count = (int)value_array_size(frame_val);
        if (frame->sprite_count > MENU_MAX_SPRITES_PER_FRAME)
            frame->sprite_count = MENU_MAX_SPRITES_PER_FRAME;
        for (int i = 0; i < frame->sprite_count; i++)
        {
            VALUE *elem = value_array_get(frame_val, i);
            frame->sprites[i] = (short)value_int32(elem);
        }
        return true;
    }
    return false;
}

static TbBool parse_visual_state(VALUE *state_obj, struct VisualState *state)
{
    memset(state, 0, sizeof(*state));
    if (state_obj == NULL || value_type(state_obj) != VALUE_DICT)
        return false;

    state->font_index = (unsigned char)json_get_int(state_obj, "font_index", 0);
    state->frame_duration_ms = json_get_int(state_obj, "frame_duration_ms", 100);

    VALUE *sprites = value_dict_get(state_obj, "sprites");
    if (sprites != NULL && value_type(sprites) == VALUE_ARRAY)
    {
        state->frame_count = 1;
        parse_sprite_frame(sprites, &state->frames[0]);
        return true;
    }

    VALUE *anim = value_dict_get(state_obj, "animation");
    if (anim != NULL && value_type(anim) == VALUE_ARRAY)
    {
        state->frame_count = (int)value_array_size(anim);
        if (state->frame_count > MENU_MAX_ANIM_FRAMES)
            state->frame_count = MENU_MAX_ANIM_FRAMES;
        for (int i = 0; i < state->frame_count; i++)
        {
            VALUE *frame_obj = value_array_get(anim, i);
            VALUE *frame_sprites = value_dict_get(frame_obj, "sprites");
            if (frame_sprites != NULL)
                parse_sprite_frame(frame_sprites, &state->frames[i]);
        }
        return true;
    }
    return false;
}

static void parse_visual_definition(VALUE *visual_obj, struct VisualDefinition *visual)
{
    memset(visual, 0, sizeof(*visual));
    if (visual_obj == NULL || value_type(visual_obj) != VALUE_DICT)
        return;

    const char *comp_str = json_get_string(visual_obj, "composition");
    visual->composition = parse_composition(comp_str);

    VALUE *states = value_dict_get(visual_obj, "states");
    if (states != NULL && value_type(states) == VALUE_DICT)
    {
        parse_visual_state(value_dict_get(states, "default"), &visual->state_default);
        parse_visual_state(value_dict_get(states, "hover"), &visual->state_hover);
        parse_visual_state(value_dict_get(states, "disabled"), &visual->state_disabled);
        parse_visual_state(value_dict_get(states, "pressed"), &visual->state_pressed);
    }
}

static void parse_callback(VALUE *cb_obj, struct CallbackDefinition *cb)
{
    memset(cb, 0, sizeof(*cb));
    if (cb_obj == NULL)
        return;

    if (value_type(cb_obj) == VALUE_STRING)
    {
        strncpy(cb->name, value_string(cb_obj), sizeof(cb->name) - 1);
        cb->is_lua = false;
        return;
    }

    if (value_type(cb_obj) == VALUE_DICT)
    {
        const char *type = json_get_string(cb_obj, "type");
        const char *script = json_get_string(cb_obj, "script");
        const char *func_name = json_get_string(cb_obj, "function");

        if (type != NULL && strcmp(type, "lua") == 0 && script != NULL)
        {
            strncpy(cb->name, script, sizeof(cb->name) - 1);
            cb->is_lua = true;
        }
        else if (func_name != NULL)
        {
            strncpy(cb->name, func_name, sizeof(cb->name) - 1);
            cb->is_lua = false;
        }
    }
}

// Refactor common code for parsing buttons, sliders, edit boxes, and hotspots since they share a lot of properties and this will make it easier to add new types in the future,
// I would like an interface pattern for this and define a parse function on the button type, but that's overkill atm
static enum TbButtonType parse_button_type(const char *str)
{
    if (str == NULL)                         return LbBtnT_NormalBtn;
    if (strcmp(str, "NormalBtn") == 0)        return LbBtnT_NormalBtn;
    if (strcmp(str, "HoldableBtn") == 0)      return LbBtnT_HoldableBtn;
    if (strcmp(str, "ToggleBtn") == 0)        return LbBtnT_ToggleBtn;
    if (strcmp(str, "RadioBtn") == 0)         return LbBtnT_RadioBtn;
    if (strcmp(str, "HorizSlider") == 0)      return LbBtnT_HorizSlider;
    if (strcmp(str, "EditBox") == 0)          return LbBtnT_EditBox;
    if (strcmp(str, "Hotspot") == 0)          return LbBtnT_Hotspot;
    WARNLOG("Unknown button type: \"%s\"", str);
    return LbBtnT_NormalBtn;
}

static TbBool parse_button(VALUE *btn_obj, struct ButtonDefinition *btn)
{
    memset(btn, 0, sizeof(*btn));

    const char *id = json_get_string(btn_obj, "id");
    if (id != NULL)
        strncpy(btn->id, id, sizeof(btn->id) - 1);

    const char *type_str = json_get_string(btn_obj, "type");
    btn->type = parse_button_type(type_str);

    btn->id_num = (short)json_get_int(btn_obj, "id_num", 0);
    btn->btype_value = (unsigned short)json_get_int(btn_obj, "btype_value", 0);

    VALUE *pos = value_dict_get(btn_obj, "position");
    if (pos != NULL && value_type(pos) == VALUE_DICT)
    {
        VALUE *px = value_dict_get(pos, "x");
        VALUE *py = value_dict_get(pos, "y");

        if (px != NULL && value_type(px) == VALUE_STRING && strcmp(value_string(px), "center") == 0)
            btn->pos_x = POS_SCRCTR;
        else
            btn->pos_x = json_get_int(pos, "x", 0);

        if (py != NULL && value_type(py) == VALUE_STRING && strcmp(value_string(py), "center") == 0)
            btn->pos_y = POS_SCRCTR;
        else
            btn->pos_y = json_get_int(pos, "y", 0);
    }

    VALUE *size = value_dict_get(btn_obj, "size");
    if (size != NULL && value_type(size) == VALUE_DICT)
    {
        btn->width = json_get_int(size, "width", 0);
        btn->height = json_get_int(size, "height", 0);
    }

    VALUE *visual = value_dict_get(btn_obj, "visual");
    parse_visual_definition(visual, &btn->visual);

    VALUE *text = value_dict_get(btn_obj, "text");
    if (text != NULL && value_type(text) == VALUE_DICT)
    {
        btn->text_string_id = (TextStringId)json_get_int(text, "string_id", 0);
    }

    VALUE *state = value_dict_get(btn_obj, "state");
    if (state != NULL && value_type(state) == VALUE_DICT)
    {
        btn->enabled = json_get_bool(state, "enabled", true);
        btn->visible = json_get_bool(state, "visible", true);
        btn->clickable = json_get_bool(state, "clickable", true);
    }
    else
    {
        btn->enabled = true;
        btn->visible = true;
        btn->clickable = true;
    }

    VALUE *callbacks = value_dict_get(btn_obj, "callbacks");
    if (callbacks != NULL && value_type(callbacks) == VALUE_DICT)
    {
        parse_callback(value_dict_get(callbacks, "on_click"), &btn->on_click);
        parse_callback(value_dict_get(callbacks, "on_rclick"), &btn->on_rclick);
        parse_callback(value_dict_get(callbacks, "on_hover"), &btn->on_hover);
        parse_callback(value_dict_get(callbacks, "on_draw"), &btn->on_draw);
        parse_callback(value_dict_get(callbacks, "on_maintain"), &btn->on_maintain);
    }

    btn->content_value = (long)json_get_int(btn_obj, "content_value", 0);
    btn->text[0] = '\0';
    btn->font_index = 1;

    const char *freetext = json_get_string(btn_obj, "text");
    if (freetext != NULL)
    {
        strncpy(btn->text, freetext, sizeof(btn->text) - 1);
        btn->text[sizeof(btn->text) - 1] = '\0';
        btn->font_index = (unsigned char)json_get_int(btn_obj, "font_index", 1);
        btn->content_name[0] = '\0';
    }
    else
    {
        const char *cname = json_get_string(btn_obj, "content_name");
        if (cname != NULL)
        {
            strncpy(btn->content_name, cname, sizeof(btn->content_name) - 1);
            btn->content_name[sizeof(btn->content_name) - 1] = '\0';
            long resolved = JsonParser::ResolveContentName(cname);
            if (resolved >= 0)
                btn->content_value = resolved;
        }
        else
        {
            btn->content_name[0] = '\0';
        }
    }
    btn->maxval = (short)json_get_int(btn_obj, "maxval", 0);
    btn->tooltip_stridx = (short)json_get_int(btn_obj, "tooltip_stridx", GUIStr_Empty);

    const char *nav_to = json_get_string(btn_obj, "navigate_to");
    if (nav_to != NULL)
        strncpy(btn->navigate_to, nav_to, sizeof(btn->navigate_to) - 1);
    else
        btn->navigate_to[0] = '\0';

    const char *dyntext = json_get_string(btn_obj, "dynamic_text");
    if (dyntext != NULL)
    {
        strncpy(btn->dynamic_text, dyntext, sizeof(btn->dynamic_text) - 1);
        btn->dynamic_text[sizeof(btn->dynamic_text) - 1] = '\0';
        btn->font_index = (unsigned char)json_get_int(btn_obj, "font_index", 1);
    }
    else
    {
        btn->dynamic_text[0] = '\0';
    }

    return true;
}

/******************************************************************************/
/* JsonParser implementation                                                  */
/******************************************************************************/

JsonParser& JsonParser::GetInstance()
{
    static JsonParser instance;
    return instance;
}

long JsonParser::ResolveContentName(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return -1;

    for (int i = 0; s_contentNames[i].name != NULL; i++)
    {
        if (strcmp(s_contentNames[i].name, name) == 0)
            return s_contentNames[i].value;
    }
    WARNLOG("Unknown content name: \"%s\"", name);
    return -1;
}

TbBool JsonParser::LoadMenuFromJson(const char *filepath, struct MenuDefinition *menuDef) const
{
    memset(menuDef, 0, sizeof(*menuDef));

    long fsize = LbFileLength(filepath);
    if (fsize <= 0)
    {
        ERRORLOG("Cannot read menu file: \"%s\"", filepath);
        return false;
    }

    char *buffer = (char *)malloc(fsize + 1);
    if (buffer == NULL)
    {
        ERRORLOG("Cannot allocate %ld bytes for menu file", fsize);
        return false;
    }

    TbFileHandle fh = LbFileOpen(filepath, Lb_FILE_MODE_READ_ONLY);
    if (fh == NULL)
    {
        ERRORLOG("Failed to open menu file: \"%s\"", filepath);
        free(buffer);
        return false;
    }
    if (LbFileRead(fh, buffer, fsize) != (int)fsize)
    {
        ERRORLOG("Failed to read menu file: \"%s\"", filepath);
        LbFileClose(fh);
        free(buffer);
        return false;
    }
    LbFileClose(fh);
    buffer[fsize] = '\0';

    VALUE root = {};
    JSON_INPUT_POS json_pos = {};
    int ret = json_dom_parse(buffer, fsize, NULL, 0, &root, &json_pos);
    free(buffer);

    if (ret != 0)
    {
        ERRORLOG("JSON parse error in \"%s\" at line %d col %d",
                 filepath, json_pos.line_number, json_pos.column_number);
        return false;
    }

    if (value_type(&root) != VALUE_DICT)
    {
        ERRORLOG("Menu JSON root must be an object: \"%s\"", filepath);
        value_fini(&root);
        return false;
    }

    const char *menu_id = json_get_string(&root, "menu_id");
    if (menu_id != NULL)
        strncpy(menuDef->menu_id, menu_id, sizeof(menuDef->menu_id) - 1);

    const char *desc = json_get_string(&root, "description");
    if (desc != NULL)
        strncpy(menuDef->description, desc, sizeof(menuDef->description) - 1);

    VALUE *pos = value_dict_get(&root, "position");
    if (pos != NULL && value_type(pos) == VALUE_DICT)
    {
        VALUE *px = value_dict_get(pos, "x");
        if (px != NULL && value_type(px) == VALUE_STRING && strcmp(value_string(px), "center") == 0)
        {
            menuDef->position_centered = true;
            menuDef->pos_x = POS_SCRCTR;
            menuDef->pos_y = POS_SCRCTR;
        }
        else
        {
            menuDef->pos_x = json_get_int(pos, "x", 0);
            menuDef->pos_y = json_get_int(pos, "y", 0);
        }
    }

    VALUE *size = value_dict_get(&root, "size");
    if (size != NULL && value_type(size) == VALUE_DICT)
    {
        menuDef->width = json_get_int(size, "width", 640);
        menuDef->height = json_get_int(size, "height", 480);
    }

    menuDef->is_monopoly_menu = json_get_bool(&root, "is_monopoly_menu", false);
    parse_callback(value_dict_get(&root, "draw_cb"), &menuDef->draw_cb);
    parse_callback(value_dict_get(&root, "create_cb"), &menuDef->create_cb);

    VALUE *buttons = value_dict_get(&root, "buttons");
    if (buttons != NULL && value_type(buttons) == VALUE_ARRAY)
    {
        menuDef->button_count = (int)value_array_size(buttons);
        if (menuDef->button_count > MENU_MAX_BUTTONS)
        {
            WARNLOG("Menu \"%s\" has %d buttons, capping at %d",
                    menuDef->menu_id, menuDef->button_count, MENU_MAX_BUTTONS);
            menuDef->button_count = MENU_MAX_BUTTONS;
        }
        for (int i = 0; i < menuDef->button_count; i++)
        {
            VALUE *btn_obj = value_array_get(buttons, i);
            if (btn_obj != NULL && value_type(btn_obj) == VALUE_DICT)
            {
                if (!parse_button(btn_obj, &menuDef->buttons[i]))
                    WARNLOG("Failed to parse button %d in menu \"%s\"", i, menuDef->menu_id);
            }
        }
    }

    VALUE *scripts = value_dict_get(&root, "lua_scripts");
    if (scripts != NULL && value_type(scripts) == VALUE_ARRAY)
    {
        menuDef->lua_script_count = (int)value_array_size(scripts);
        if (menuDef->lua_script_count > MENU_MAX_LUA_SCRIPTS)
            menuDef->lua_script_count = MENU_MAX_LUA_SCRIPTS;
        for (int i = 0; i < menuDef->lua_script_count; i++)
        {
            VALUE *script = value_array_get(scripts, i);
            if (script != NULL && value_type(script) == VALUE_STRING)
                strncpy(menuDef->lua_scripts[i], value_string(script), sizeof(menuDef->lua_scripts[i]) - 1);
        }
    }

    SYNCDBG(5, "Loaded menu \"%s\" with %d buttons from \"%s\"",
            menuDef->menu_id, menuDef->button_count, filepath);

    value_fini(&root);
    return true;
}
