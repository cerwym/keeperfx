/******************************************************************************/
// ui_init.h: Decoupled UI initialization and management
// Handles pre-allocation of UI menus based on player role
/******************************************************************************/
#ifndef UI_INIT_H
#define UI_INIT_H

#include "player_data.h"  // For UIPlayerRole enum

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
// Forward declarations for types
/******************************************************************************/

struct PlayerInfo;  // Forward declaration

/******************************************************************************/
// Function Declarations
/******************************************************************************/

/**
 * Initialize UI configuration for a specific player
 * @param player_num The player index to configure
 * @param role The UIPlayerRole for this player
 */
void init_player_ui_config(unsigned char player_num, enum UIPlayerRole role);

/**
 * Initialize all gameplay UI menus based on local player role
 * Pre-allocates all menus and buttons before gameplay loop starts
 * @param local_player_role The UI role of the local player
 * @param multiplayer Whether this is a multiplayer session
 * @return non-zero on success, zero on failure
 */
unsigned char init_gameplay_ui(enum UIPlayerRole local_player_role, unsigned char multiplayer);

/**
 * Get the list of menus applicable to a given player role
 * @param role The UIPlayerRole to query
 * @param out_count Pointer to store the count of menus
 * @return Array of MenuID values, or NULL if role has no menus
 */
unsigned short *get_ui_menu_list_for_role(enum UIPlayerRole role, int *out_count);

/**
 * Check if UI has been initialized
 * @return non-zero if init_gameplay_ui() was called, zero otherwise
 */
unsigned char is_ui_initialized(void);

/**
 * Check if UI rendering is currently enabled
 * @return non-zero if UI should be rendered, zero otherwise
 * (returns 1 only if BOTH ui_initialized AND ui_rendering_enabled)
 */
unsigned char should_render_ui(void);

/**
 * Set UI rendering on or off
 * Safe to toggle without crashing - menus are pre-allocated
 * @param enabled non-zero to render UI, zero to skip UI rendering
 */
void set_ui_rendering_enabled(unsigned char enabled);

#ifdef __cplusplus
}
#endif

#endif  // UI_INIT_H
