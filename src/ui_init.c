/******************************************************************************/
// ui_init.c: UI initialization and player-role configuration.
// Menus are created on-demand by turn_on_menu() / create_menu() as the player
// opens panels.  init_gameplay_ui() only records the player's role; it does
// NOT pre-create menus, keeping within the ACTIVE_MENUS_COUNT=8 slot limit.
/******************************************************************************/

#include "globals.h"
#include "gui_frontmenu.h"
#include "player_data.h"
#include "ui_init.h"

/******************************************************************************/
// Global State
/******************************************************************************/

/** Tracks whether UI initialization has been completed */
static unsigned char ui_initialized = 0;

/** Global flag to control UI rendering */
static unsigned char ui_rendering_enabled = 1;

/******************************************************************************/
// Public Function Implementations
/******************************************************************************/

void init_player_ui_config(unsigned char player_num, enum UIPlayerRole role)
{
    struct PlayerInfo *player = get_player(player_num);
    if (player == NULL) {
        ERRORLOG("Invalid player number: %d", player_num);
        return;
    }

    player->ui_config.role = role;
    player->ui_config.ui_menus_initialized = 0;

    SYNCDBG(3, "Initialized UI config for player %d with role %d", player_num, (int)role);
}

unsigned char init_gameplay_ui(enum UIPlayerRole local_player_role, unsigned char multiplayer)
{
    SYNCDBG(2, "Initializing gameplay UI - local_role=%d, multiplayer=%d",
            (int)local_player_role, (int)multiplayer);

    // Record this player's UI role.  Menus are created lazily by turn_on_menu()
    // as the player opens panels — we do NOT call create_menu() here.
    init_player_ui_config(my_player_number, local_player_role);
    get_player(my_player_number)->ui_config.ui_menus_initialized = 1;

    if (multiplayer) {
        for (unsigned char i = 0; i < PLAYERS_COUNT; i++) {
            if (i == my_player_number)
                continue;

            struct PlayerInfo *other_player = get_player(i);
            if (other_player == NULL || !other_player->is_active)
                continue;

            init_player_ui_config(i, UIPROLE_ACTIVE_PLAYER);
            other_player->ui_config.ui_menus_initialized = 1;
        }
    }

    SYNCDBG(2, "Gameplay UI initialization complete");
    ui_initialized = 1;
    return 1;
}

unsigned char should_render_ui(void)
{
    return ui_initialized && ui_rendering_enabled;
}

unsigned char is_ui_initialized(void)
{
    return ui_initialized;
}

void set_ui_rendering_enabled(unsigned char enabled)
{
    ui_rendering_enabled = (enabled != 0) ? 1 : 0;
    SYNCDBG(3, "UI rendering %s", enabled ? "enabled" : "disabled");
}

/******************************************************************************/
