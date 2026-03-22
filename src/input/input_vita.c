/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file input_vita.c
 *     PlayStation Vita input implementation.
 * @par Purpose:
 *     Provides input interface implementation for PS Vita controls.
 * @par Comment:
 *     Maps Vita buttons, analog sticks, and touchscreen to game controls.
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
#include "../pre_inc.h"
#include "input_interface.h"

#ifdef PLATFORM_VITA

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string.h>

#include "../bflib_keybrd.h"
#include "../post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// Button mapping from Vita to KeeperFX keycodes
static const struct {
    uint32_t btn_vita;
    int keycode;
} s_buttonMap[] = {
    { SCE_CTRL_CROSS,    KC_RETURN },      // Cross = Enter
    { SCE_CTRL_CIRCLE,   KC_ESCAPE },      // Circle = Escape
    { SCE_CTRL_SQUARE,   KC_SPACE },       // Square = Space
    { SCE_CTRL_TRIANGLE, KC_TAB },         // Triangle = Tab
    { SCE_CTRL_L1,       KC_LSHIFT },      // L trigger = Left Shift
    { SCE_CTRL_R1,       KC_LCONTROL },    // R trigger = Left Control
    { SCE_CTRL_START,    KC_ESCAPE },      // Start = Escape
    { SCE_CTRL_SELECT,   KC_M },           // Select = M (map)
    { SCE_CTRL_UP,       KC_UP },          // D-pad Up
    { SCE_CTRL_DOWN,     KC_DOWN },        // D-pad Down
    { SCE_CTRL_LEFT,     KC_LEFT },        // D-pad Left
    { SCE_CTRL_RIGHT,    KC_RIGHT },       // D-pad Right
};

#define BUTTON_MAP_SIZE (sizeof(s_buttonMap) / sizeof(s_buttonMap[0]))

/******************************************************************************/
// Global state
static SceCtrlData s_padData;
static SceTouchData s_touchDataFront;
static SceTouchData s_touchDataBack;
static bool s_touchActive = false;
static int s_mouseX = 480;  // Center of 960x544 screen
static int s_mouseY = 272;
static int s_mouseButtons = 0;

// Virtual keyboard state
static unsigned char s_keyState[KC_LIST_END];

/******************************************************************************/
// Helper functions

static void update_key_states(void)
{
    // Compute new key state by OR-ing all buttons that share a keycode
    unsigned char new_state[KC_LIST_END];
    memset(new_state, 0, sizeof(new_state));
    for (int i = 0; i < BUTTON_MAP_SIZE; i++) {
        if (s_padData.buttons & s_buttonMap[i].btn_vita) {
            new_state[s_buttonMap[i].keycode] = 1;
        }
    }
    // Fire keyboardControl() transitions so lbKeyOn[] stays in sync.
    // Iterate by button (not by keycode) to cover all entries; update
    // s_keyState inline so shared-keycode duplicates are suppressed.
    for (int i = 0; i < BUTTON_MAP_SIZE; i++) {
        int keycode = s_buttonMap[i].keycode;
        if (new_state[keycode] != s_keyState[keycode]) {
            keyboardControl(new_state[keycode] ? KActn_KEYDOWN : KActn_KEYUP,
                            (TbKeyCode)keycode, KMod_NONE, 0);
            s_keyState[keycode] = new_state[keycode]; // suppress duplicate for same keycode
        }
    }
}

static void update_mouse_from_touch(void)
{
    // Front touchscreen for absolute positioning
    if (s_touchDataFront.reportNum > 0) {
        s_touchActive = true;

        // Convert touch coordinates (1920x1088) to screen space (960x544)
        s_mouseX = s_touchDataFront.report[0].x / 2;
        s_mouseY = s_touchDataFront.report[0].y / 2;

        s_mouseButtons = INPUT_MOUSE_BUTTON_LEFT;
    } else {
        if (s_touchActive) {
            // Touch released
            s_mouseButtons = 0;
            s_touchActive = false;
        }
    }

    // Back touchpad for additional controls (optional)
    if (s_touchDataBack.reportNum > 0) {
        // Could map back touch to different mouse buttons or gestures
        s_mouseButtons |= INPUT_MOUSE_BUTTON_RIGHT;
    }
}

static void update_cursor_from_analog(void)
{
    // Use left analog stick for cursor movement — only when no touch active
    int lx = s_padData.lx - 128;  // Center at 0
    int ly = s_padData.ly - 128;

    // Apply deadzone
    const int deadzone = 30;
    if (abs(lx) < deadzone) lx = 0;
    if (abs(ly) < deadzone) ly = 0;

    if (lx != 0 || ly != 0) {
        s_mouseX += lx / 20;
        s_mouseY += ly / 20;

        // Clamp to screen bounds
        if (s_mouseX < 0) s_mouseX = 0;
        if (s_mouseX >= 960) s_mouseX = 959;
        if (s_mouseY < 0) s_mouseY = 0;
        if (s_mouseY >= 544) s_mouseY = 543;
    }
}

static void update_buttons_as_mouse_clicks(void)
{
    // Face buttons always produce mouse clicks regardless of touch state.
    // The main menu (and most UI) is mouse-click driven, not keyboard-driven.
    if (s_padData.buttons & SCE_CTRL_CROSS) {
        s_mouseButtons |= INPUT_MOUSE_BUTTON_LEFT;
    }
    if (s_padData.buttons & SCE_CTRL_CIRCLE) {
        s_mouseButtons |= INPUT_MOUSE_BUTTON_RIGHT;
    }
}

/******************************************************************************/
// Input interface implementation

static void input_vita_poll_events(void)
{
    // Read controller state
    sceCtrlPeekBufferPositive(0, &s_padData, 1);

    // Read touch state
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &s_touchDataFront, 1);
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &s_touchDataBack, 1);

    // Update virtual keyboard state
    update_key_states();

    // Update mouse state
    s_mouseButtons = 0;
    update_mouse_from_touch();
    if (!s_touchActive) {
        update_cursor_from_analog();  // Touch takes priority for cursor position
    }
    update_buttons_as_mouse_clicks(); // Buttons always fire regardless of touch
}

static TbBool input_vita_is_key_down(int keycode)
{
    if (keycode < 0 || keycode >= KC_LIST_END)
        return false;
    return s_keyState[keycode];
}

static void input_vita_get_mouse(int* x, int* y, int* buttons)
{
    if (x != NULL)
        *x = s_mouseX;
    if (y != NULL)
        *y = s_mouseY;
    if (buttons != NULL)
        *buttons = s_mouseButtons;
}

// Exposed for WindowSystemVita::PollInput() — returns the current virtual
// cursor position maintained by analog-stick and front-touchscreen logic.
void vita_get_virtual_cursor(int* x, int* y)
{
    if (x != NULL)
        *x = s_mouseX;
    if (y != NULL)
        *y = s_mouseY;
}

static TbBool input_vita_get_gamepad_axis(int axis, int* value)
{
    if (value == NULL)
        return false;

    int raw_value = 0;
    bool valid = false;

    switch (axis) {
        case 0:  // Left X axis
            raw_value = s_padData.lx - 128;
            valid = true;
            break;
        case 1:  // Left Y axis
            raw_value = -(s_padData.ly - 128);  // Invert Y
            valid = true;
            break;
        case 2:  // Right X axis
            raw_value = s_padData.rx - 128;
            valid = true;
            break;
        case 3:  // Right Y axis
            raw_value = -(s_padData.ry - 128);  // Invert Y
            valid = true;
            break;
        default:
            *value = 0;
            return false;
    }

    // Apply deadzone
    const int deadzone = 30;
    if (abs(raw_value) < deadzone)
        raw_value = 0;

    // Scale to full range (-32768 to 32767)
    *value = raw_value * 256;
    return valid;
}

static InputInterface input_vita_impl = {
    input_vita_poll_events,
    input_vita_is_key_down,
    input_vita_get_mouse,
    input_vita_get_gamepad_axis,
};

void input_vita_initialize(void)
{
    // Set sampling mode for controller
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    // Enable touch
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

    // Initialize input state
    memset(s_keyState, 0, KC_LIST_END);
    s_mouseX = 480;  // Center of screen
    s_mouseY = 272;
    s_mouseButtons = 0;

    g_input = &input_vita_impl;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // PLATFORM_VITA
