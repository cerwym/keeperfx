/******************************************************************************/
// ui_init.h: UI initialization and player-role configuration.
/******************************************************************************/
#ifndef UI_INIT_H
#define UI_INIT_H

#include "player_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct PlayerInfo;

/**
 * Record UI role for a player.  Does not create any menus.
 */
void init_player_ui_config(unsigned char player_num, enum UIPlayerRole role);

/**
 * Record UI roles for all active players and mark the UI as ready.
 * Menus are created on-demand by turn_on_menu() as the player opens panels.
 * @param local_player_role  Role for the local (human) player.
 * @param multiplayer        Non-zero when more than one player is active.
 * @return Non-zero on success.
 */
unsigned char init_gameplay_ui(enum UIPlayerRole local_player_role, unsigned char multiplayer);

/** Returns non-zero after init_gameplay_ui() has been called. */
unsigned char is_ui_initialized(void);

/** Returns non-zero when UI is both initialized and rendering is enabled. */
unsigned char should_render_ui(void);

/** Enable or disable UI rendering without touching menu slot state. */
void set_ui_rendering_enabled(unsigned char enabled);

#ifdef __cplusplus
}
#endif

#endif  // UI_INIT_H
