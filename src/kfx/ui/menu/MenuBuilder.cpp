/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuBuilder.cpp
 *     Converts MenuDefinition → GuiButtonInit[]/GuiMenu.
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

#include "MenuBuilder.h"
#include "CallbackRegistry.h"
#include "FreeformText.h"
#include "DynamicText.h"

#include "../../../bflib_basics.h"
#include "../../../gui_draw.h"
#include "../../../vidmode.h"

#include "../../../post_inc.h"

#include <string.h>
#include <stdlib.h>

/* Forward declarations for draw callbacks */
extern "C" void frontend_draw_large_freetext_button(struct GuiButton *gbtn);
extern "C" void frontend_draw_large_dyntext_button(struct GuiButton *gbtn);

struct GuiButtonInit* MenuBuilder::BuildButtonInitArray(const struct MenuDefinition *menuDef)
{
    if (menuDef == NULL)
    {
        ERRORLOG("BuildButtonInitArray: NULL menuDef");
        return NULL;
    }
    if (menuDef->button_count <= 0)
    {
        WARNLOG("BuildButtonInitArray: menu \"%s\" has no buttons", menuDef->menu_id);
    }

    int count = menuDef->button_count + 1;
    struct GuiButtonInit *arr = (struct GuiButtonInit *)calloc(count, sizeof(struct GuiButtonInit));
    if (arr == NULL)
    {
        ERRORLOG("Cannot allocate %d GuiButtonInit entries", count);
        return NULL;
    }

    CallbackRegistry &callbacks = CallbackRegistry::GetInstance();
    FreeformText &freetext = FreeformText::GetInstance();

    for (int i = 0; i < menuDef->button_count; i++)
    {
        const struct ButtonDefinition *bdef = &menuDef->buttons[i];
        struct GuiButtonInit *gi = &arr[i];

        gi->gbtype = (char)bdef->type;
        gi->id_num = bdef->id_num;
        gi->unused_field = 0;
        gi->button_flags = 0;

        gi->click_event = callbacks.Resolve(bdef->on_click.name);
        gi->rclick_event = callbacks.Resolve(bdef->on_rclick.name);
        gi->ptover_event = callbacks.Resolve(bdef->on_hover.name);
        gi->draw_call = callbacks.Resolve(bdef->on_draw.name);
        gi->maintain_call = callbacks.Resolve(bdef->on_maintain.name);

        // Log if a named callback failed to resolve (helps diagnose missing registrations)
        if (bdef->on_click.name[0] != '\0' && gi->click_event == NULL)
            WARNLOG("Button \"%s\": on_click \"%s\" unresolved", bdef->id, bdef->on_click.name);
        if (bdef->on_draw.name[0] != '\0' && gi->draw_call == NULL)
            WARNLOG("Button \"%s\": on_draw \"%s\" unresolved", bdef->id, bdef->on_draw.name);
        if (bdef->on_maintain.name[0] != '\0' && gi->maintain_call == NULL)
            WARNLOG("Button \"%s\": on_maintain \"%s\" unresolved", bdef->id, bdef->on_maintain.name);

        gi->btype_value = bdef->btype_value;
        gi->scr_pos_x = (short)bdef->pos_x;
        gi->scr_pos_y = (short)bdef->pos_y;
        gi->pos_x = (short)bdef->pos_x;
        gi->pos_y = (short)bdef->pos_y;
        gi->width = (short)bdef->width;
        gi->height = (short)bdef->height;

        if (bdef->visual.state_default.frame_count > 0 &&
            bdef->visual.state_default.frames[0].sprite_count > 0)
        {
            gi->sprite_idx = bdef->visual.state_default.frames[0].sprites[0];
        }
        else
        {
            gi->sprite_idx = 0;
        }

        gi->tooltip_stridx = bdef->tooltip_stridx;
        gi->parent_menu = NULL;

        /* Priority: dynamic_text > freeform text > content_name/content_value */
        // TODO : confirm localisation support for dynamic/freetext, currently they just bypass the engine's frontend_button_info[] lookup which is used for localization, but we may want to add support for looking up a string ID in the future, don't tell Loobinex
        if (bdef->dynamic_text[0] != '\0')
        {
            DynamicText &dyntext = DynamicText::GetInstance();
            long dyntext_val = dyntext.Register(bdef->dynamic_text, bdef->font_index);
            if (dyntext_val >= 0)
            {
                gi->content.lval = dyntext_val;
                gi->draw_call = (Gf_Btn_Callback)frontend_draw_large_dyntext_button;
            }
            else
            {
                gi->content.lval = bdef->content_value;
            }
        }
        else if (bdef->text[0] != '\0')
        {
            long freetext_val = freetext.Register(bdef->text, bdef->font_index);
            if (freetext_val >= 0)
            {
                gi->content.lval = freetext_val;
                gi->draw_call = (Gf_Btn_Callback)frontend_draw_large_freetext_button;
            }
            else
            {
                gi->content.lval = bdef->content_value;
            }
        }
        else
        {
            gi->content.lval = bdef->content_value;
        }

        gi->maxval = bdef->maxval;
    }

    /* Sentinel terminator — engine expects gbtype == -1 */
    arr[menuDef->button_count].gbtype = -1;
    arr[menuDef->button_count].id_num = 0;

    return arr;
}

void MenuBuilder::BuildGuiMenu(const struct MenuDefinition *menuDef,
                                struct GuiButtonInit *buttons,
                                struct GuiMenu *menuOut)
{
    if (menuDef == NULL || buttons == NULL || menuOut == NULL)
    {
        ERRORLOG("BuildGuiMenu: NULL parameter (menuDef=%p buttons=%p menuOut=%p)",
                 (void*)menuDef, (void*)buttons, (void*)menuOut);
        return;
    }
    memset(menuOut, 0, sizeof(*menuOut));

    menuOut->ident = 0;
    menuOut->visual_state = 0;
    menuOut->fade_time = 1.0f;
    menuOut->buttons = buttons;

    if (menuDef->position_centered)
    {
        menuOut->pos_x = POS_SCRCTR;
        menuOut->pos_y = POS_SCRCTR;
    }
    else
    {
        menuOut->pos_x = (short)menuDef->pos_x;
        menuOut->pos_y = (short)menuDef->pos_y;
    }

    menuOut->width = (short)menuDef->width;
    menuOut->height = (short)menuDef->height;
    menuOut->draw_cb = NULL;
    menuOut->number = 0;
    menuOut->menu_init = NULL;
    menuOut->create_cb = NULL;
    menuOut->is_turned_on = 0;
    menuOut->is_monopoly_menu = menuDef->is_monopoly_menu ? 1 : 0;
    menuOut->is_active_panel = 0;
}

void MenuBuilder::FreeArray(struct GuiButtonInit *buttons)
{
    if (buttons != NULL)
        free(buttons);
}
