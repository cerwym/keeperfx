/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_guibtns.c
 *     GUI Buttons support.
 * @par Purpose:
 *     Definition of button, and common routines to handle it.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 Nov 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_guibtns.h"

#include <string.h>
#include <stdio.h>

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_string.h"
#include "bflib_sound.h"
#include "bflib_keybrd.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
// Global variables
TbCharCount input_field_pos;
struct GuiButton *input_button;
char backup_input_field[INPUT_FIELD_LEN];

/******************************************************************************/
// Functions

/**
 * Checks if given position is over a specific button.
 * @param gbtn The button which position is to be verified.
 * @param pos_x The on-screen position X coord.
 * @param pos_y The on-screen position Y coord.
 * @return Returns true it position is over the button.
 */
TbBool check_if_pos_is_over_button(const struct GuiButton *gbtn, TbScreenPos pos_x, TbScreenPos pos_y)
{
    TbScreenPos x = gbtn->pos_x;
    TbScreenPos y = gbtn->pos_y;
    // Per-pixel mask takes priority when available
    if (gbtn->hit_mask)
    {
        // Use the sprite's actual draw position (scr_pos) and rendered size
        TbScreenPos sx0 = gbtn->scr_pos_x;
        TbScreenPos sy0 = gbtn->scr_pos_y;
        int rw = (gbtn->hit_mask_render_w > 0) ? gbtn->hit_mask_render_w : gbtn->width;
        int rh = (gbtn->hit_mask_render_h > 0) ? gbtn->hit_mask_render_h : gbtn->height;
        // Fast AABB reject against rendered sprite rect
        if (pos_x < sx0 || pos_x >= sx0 + rw || pos_y < sy0 || pos_y >= sy0 + rh)
            return false;
        int bx = (int)(pos_x - sx0);
        int by = (int)(pos_y - sy0);
        // Map rendered-pixel coords to native sprite coords
        int sx = (gbtn->hit_mask_w > 0) ? bx * gbtn->hit_mask_w / rw : 0;
        int sy = (gbtn->hit_mask_h > 0) ? by * gbtn->hit_mask_h / rh : 0;
        if (sx < 0 || sy < 0 || sx >= gbtn->hit_mask_w || sy >= gbtn->hit_mask_h) return false;
        int byte_idx = sy * gbtn->hit_mask_stride + (sx >> 3);
        return (gbtn->hit_mask[byte_idx] >> (sx & 7)) & 1;
    }
    if (gbtn->hit_shape == -1)
    {
        // Inscribed ellipse: (nx/a)^2 + (ny/b)^2 <= 1, evaluated with fixed-point scale
        int32_t a = gbtn->width / 2;
        int32_t b = gbtn->height / 2;
        if (a <= 0 || b <= 0)
            return false;
        int32_t nx = (int32_t)pos_x - (x + a);
        int32_t ny = (int32_t)pos_y - (y + b);
        int32_t snx = nx * 256 / a;
        int32_t sny = ny * 256 / b;
        return (snx * snx + sny * sny) <= (256 * 256);
    }
    TbScreenPos inset = (gbtn->hit_shape > 0) ? (TbScreenPos)gbtn->hit_shape : 0;
    return (pos_x >= x + inset) && (pos_x < x + gbtn->width - inset)
        && (pos_y >= y + inset) && (pos_y < y + gbtn->height - inset);
}

void do_sound_menu_click(void)
{
    play_non_3d_sample_no_overlap(61);
}

void do_sound_button_click(struct GuiButton *gbtn)
{
    if (gbtn->gbtype == LbBtnT_RadioBtn)
        play_non_3d_sample(60);
    else
        play_non_3d_sample(61);
}

void setup_input_field(struct GuiButton *gbtn, const char * empty_text)
{
    lbInkey = 0;
    memset(backup_input_field, 0, INPUT_FIELD_LEN);
    char* content = gbtn->content.str;
    if (content == NULL)
    {
        ERRORLOG("Button has invalid content pointer");
        return;
    }
    snprintf(backup_input_field, INPUT_FIELD_LEN, "%s", content);
    // Check if the text drawn should be treated as empty; if it is, ignore that string
    if ((empty_text != NULL) && (strncmp(empty_text, backup_input_field, INPUT_FIELD_LEN-1) == 0))
    {
        *content = '\0';
    }
    input_field_pos = LbLocTextStringLength(content);
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
