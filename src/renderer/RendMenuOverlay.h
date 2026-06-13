/******************************************************************************/
// Dungeon Keeper - Renderer Settings Menu
/******************************************************************************/
/** @file RendMenuOverlay.h
 *     Standalone in-game renderer settings overlay: state machine, input
 *     handling and drawing.  No dependency on GuiMenu or GuiButton.
 *
 * @par Usage:
 *     - Call RendMenu_ToggleOpen() to open/close (bind to KC_F9 or similar).
 *     - Call RendMenu_HandleKey(kc) when the menu is open and a KC_* key
 *       fires; it returns 1 if the key was consumed.
 *     - Call RendMenu_Draw() once per frame, after all HUD drawing.
 *
 * @par Live-preview:
 *     Every adjustment immediately calls RendererApplySettings() so changes
 *     are visible in real time.  Settings are saved to renderer_prefs.ini
 *     when the menu is closed.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void RendMenu_Open(void);
void RendMenu_Close(void);
void RendMenu_ToggleOpen(void);
int  RendMenu_IsOpen(void);

/** Handle one key-down event.  kc is a KC_* constant (bflib_keybrd.h).
 *  Returns 1 if the key was consumed, 0 if it should fall through. */
int RendMenu_HandleKey(int kc);

/** Draw the full menu overlay.  Call after gui_draw_all_boxes(). */
void RendMenu_Draw(void);

#ifdef __cplusplus
}
#endif
