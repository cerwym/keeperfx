/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file input_interface.h
 *     Input abstraction interface for platform portability.
 * @par Purpose:
 *     Provides a platform-independent interface for input operations.
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
#ifndef INPUT_INTERFACE_H
#define INPUT_INTERFACE_H

#include "../bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Mouse button bit masks
#define INPUT_MOUSE_BUTTON_LEFT   0x01
#define INPUT_MOUSE_BUTTON_RIGHT  0x02
#define INPUT_MOUSE_BUTTON_MIDDLE 0x04

typedef struct InputInterface {
    void (*poll_events)(void);
    TbBool (*is_key_down)(int keycode);
    void (*get_mouse)(int* x, int* y, int* buttons);
    TbBool (*get_gamepad_axis)(int axis, int* value);
} InputInterface;

extern InputInterface* g_input;

void input_sdl_initialize(void);
void input_vita_initialize(void);
void input_switch_initialize(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
