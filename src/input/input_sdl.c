/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file input_sdl.c
 *     SDL-based input implementation wrapping bflib_inputctrl.
 * @par Purpose:
 *     Provides an input interface implementation using SDL.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     12 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_mouse.h"
#include "input_interface.h"

#include "bflib_inputctrl.h"
#include "bflib_keybrd.h"
#include "bflib_video.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

extern unsigned char lbKeyOn[KC_LIST_END];

static void input_sdl_poll_events(void)
{
    LbWindowsControl();
}

static TbBool input_sdl_is_key_down(int keycode)
{
    if (keycode < 0 || keycode >= KC_LIST_END)
        return false;
    return lbKeyOn[keycode];
}

static void input_sdl_get_mouse(int* x, int* y, int* buttons)
{
    if (x != NULL)
        *x = lbMouse.MouseX;
    if (y != NULL)
        *y = lbMouse.MouseY;
    if (buttons != NULL) {
        *buttons = 0;
        if (lbMouse.LeftButton)
            *buttons |= INPUT_MOUSE_BUTTON_LEFT;
        if (lbMouse.RightButton)
            *buttons |= INPUT_MOUSE_BUTTON_RIGHT;
        if (lbMouse.MiddleButton)
            *buttons |= INPUT_MOUSE_BUTTON_MIDDLE;
    }
}

static TbBool input_sdl_get_gamepad_axis(int axis, int* value)
{
    // Gamepad support in KeeperFX is more complex and integrated
    // with joystick handling in bflib_input_joyst.cpp
    // For now, this is a placeholder
    if (value != NULL)
        *value = 0;
    return false;
}

static InputInterface input_sdl_impl = {
    input_sdl_poll_events,
    input_sdl_is_key_down,
    input_sdl_get_mouse,
    input_sdl_get_gamepad_axis,
};

InputInterface* g_input = NULL;

void input_sdl_initialize(void)
{
    g_input = &input_sdl_impl;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
