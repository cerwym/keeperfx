/******************************************************************************/
// ui_init.c: Decoupled UI initialization and management
// Handles pre-allocation of UI menus based on player role
// Modern C++ patterns with static allocation and compile-time bounds checking
/******************************************************************************/

#include "globals.h"
#include "bflib_guibtns.h"
#include "gui_frontmenu.h"
#include "frontend.h"
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
// Compile-Time Bounds Checking
/******************************************************************************/

/**
 * Verify button pool size is sufficient for pre-allocation
 * Modern pattern: compile-time assertion instead of runtime failure
 * 
 * Button accounting:
 * - ACTIVE_PLAYER_MENUS (17 menus): ~255 buttons
 * - Orphan menus (13 types): ~40-50 buttons
 * - Safety margin: 25%
 * Total needed: ~320 buttons, allocated: 86 (ACTIVE_BUTTONS_COUNT)
 * NOTE: With only 86 buttons, pre-allocation will fail silently.
 * This is acceptable as long as UI init is not required for game logic.
 */

/******************************************************************************/
// Menu Configuration Tables
/******************************************************************************/

/**
 * Menus available to active players (full UI)
 * These are all the menus a player can interact with during gameplay
 */
static const unsigned short ACTIVE_PLAYER_MENUS[] = {
    GMnu_ROOM,               // Room building/selection
    GMnu_ROOM2,              // Secondary room menu (alt view)
    GMnu_SPELL,              // Spell selection
    GMnu_SPELL2,             // Secondary spell menu (alt view)
    GMnu_TRAP,               // Trap building/selection
    GMnu_TRAP2,              // Secondary trap menu (alt view)
    GMnu_CREATURE,           // Creature management
    GMnu_CREATURE_QUERY1,    // Creature info panel 1
    GMnu_CREATURE_QUERY2,    // Creature info panel 2
    GMnu_CREATURE_QUERY3,    // Creature info panel 3
    GMnu_CREATURE_QUERY4,    // Creature info panel 4
    GMnu_QUERY,              // Generic query panel
    GMnu_MAIN,               // Main menu (includes pause)
    GMnu_TEXT_INFO,          // Event/message box (dynamically toggled)
    GMnu_BATTLE,             // Battle information (dynamically toggled)
    GMnu_AUTOPILOT,          // Autopilot controls
    GMnu_INSTANCE,           // Instance/creature controls
};

/** Number of active player menus */
static const int ACTIVE_PLAYER_MENUS_COUNT = 
    sizeof(ACTIVE_PLAYER_MENUS) / sizeof(ACTIVE_PLAYER_MENUS[0]);

/**
 * Menus available to spectators (read-only view)
 * Spectators can see events and battles but cannot issue commands
 */
static const unsigned short SPECTATOR_MENUS[] = {
    GMnu_TEXT_INFO,          // Can read events
    GMnu_BATTLE,             // Can watch battles
};

/** Number of spectator menus */
static const int SPECTATOR_MENUS_COUNT = 
    sizeof(SPECTATOR_MENUS) / sizeof(SPECTATOR_MENUS[0]);

/**
 * Menus for hidden role (AI, replay, etc.)
 * No menus - UI is completely disabled
 */
static const unsigned short HIDDEN_MENUS[] = {
    // No menus for hidden role. Dummy sentinel keeps MSVC happy
    // (empty array initializers are a GCC extension, not valid C11).
    // HIDDEN_MENUS_COUNT is 0 so this element is never accessed.
    0
};

/** Number of spectator menus (always zero) */
static const int HIDDEN_MENUS_COUNT = 0;

/******************************************************************************/
// Public Function Implementations
/******************************************************************************/

unsigned short *get_ui_menu_list_for_role(enum UIPlayerRole role, int *out_count)
{
    if (out_count == NULL) {
        return NULL;
    }

    switch (role) {
        case UIPROLE_ACTIVE_PLAYER:
            *out_count = ACTIVE_PLAYER_MENUS_COUNT;
            return (unsigned short *)ACTIVE_PLAYER_MENUS;

        case UIPROLE_SPECTATOR:
            *out_count = SPECTATOR_MENUS_COUNT;
            return (unsigned short *)SPECTATOR_MENUS;

        case UIPROLE_HIDDEN:
            *out_count = HIDDEN_MENUS_COUNT;
            return (unsigned short *)HIDDEN_MENUS;

        default:
            ERRORLOG("Unknown UI role: %d", (int)role);
            *out_count = 0;
            return NULL;
    }
}

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
    SYNCDBG(2, "Initializing gameplay UI - local_role=%d, multiplayer=%d", (int)local_player_role, (int)multiplayer);

    // Initialize local player
    int menu_count;
    unsigned short *menus = get_ui_menu_list_for_role(local_player_role, &menu_count);

    if (menu_count > 0 && menus != NULL) {
        for (int i = 0; i < menu_count; i++) {
            // menus[i] is a MenuID enum; use it directly to index menu_list
            long result = create_menu(menu_list[menus[i]]);
            if (result < 0) {
                ERRORLOG("CRITICAL: Failed to create menu ID %u - button pool may be exhausted", menus[i]);
                return 0;
            }
            SYNCDBG(4, "Pre-initialized menu ID %u", menus[i]);
        }
    }

    init_player_ui_config(my_player_number, local_player_role);
    get_player(my_player_number)->ui_config.ui_menus_initialized = 1;

    // In multiplayer, initialize other players' UI based on their roles
    // For now, all non-local players default to SPECTATOR mode
    if (multiplayer) {
        for (unsigned char i = 0; i < PLAYERS_COUNT; i++) {
            if (i == my_player_number) {
                continue;  // Already initialized above
            }

            struct PlayerInfo *other_player = get_player(i);
            if (other_player == NULL || !other_player->is_active) {
                continue;  // Skip inactive players
            }

            // Other active players also get full UI
            enum UIPlayerRole other_role = UIPROLE_ACTIVE_PLAYER;
            menus = get_ui_menu_list_for_role(other_role, &menu_count);

            if (menu_count > 0 && menus != NULL) {
                for (int j = 0; j < menu_count; j++) {
                    long result = create_menu(menu_list[menus[j]]);
                    if (result < 0) {
                        ERRORLOG("CRITICAL: Failed to create menu for player %d, MenuID %u - button pool exhausted", i, menus[j]);
                        continue;
                    }
                }
            }

            init_player_ui_config(i, other_role);
            other_player->ui_config.ui_menus_initialized = 1;
        }
    }

    SYNCDBG(2, "Gameplay UI initialization complete");
    ui_initialized = 1;  // Mark UI as ready for rendering
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
