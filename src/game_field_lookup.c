/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_field_lookup.c
 *     Implementation of game field lookup resolver.
 * @par Purpose:
 *     Runtime resolution of Game struct field pointers by name.
 *     Enables lazy initialization pattern for button GUIs.
 */
/******************************************************************************/

#include "bflib_basics.h"
#include "game_field_lookup.h"
#include "game_legacy.h"
#include "globals.h"
#include <string.h>

/* gpGame points to the single Game instance defined in game_legacy.c */
#define gpGame (&game)

/******************************************************************************/

/**
 * Game struct field definitions for runtime lookup.
 * Each entry maps a field name to its offset within the Game struct.
 * 
 * MAINTENANCE NOTE:
 * If you add a new Game struct field that needs GUI access, add it here:
 * { "field_name", offsetof(struct Game, field_name), sizeof(game.field_name) }
 * 
 * These fields are referenced from static button initializers:
 * - comp_player_aggressive:  GUI button for computer assist (aggressive mode)
 * - comp_player_defensive:   GUI button for computer assist (defensive mode)
 * - comp_player_construct:   GUI button for computer assist (construction mode)
 * - comp_player_creatrsonly: GUI button for computer assist (creatures only mode)
 * - creatures_tend_imprison: GUI button for creature tendency (imprisonment)
 * - creatures_tend_flee:     GUI button for creature tendency (flee)
 * - evntbox_scroll_window:   GUI event text scroll window state
 */
static const GameFieldDef game_field_defs[] = {
    // Autopilot/Computer player settings (frontmenu_ingame_opts_data.cpp)
    {
        "comp_player_aggressive",
        offsetof(struct Game, comp_player_aggressive),
        sizeof(char)
    },
    {
        "comp_player_defensive",
        offsetof(struct Game, comp_player_defensive),
        sizeof(char)
    },
    {
        "comp_player_construct",
        offsetof(struct Game, comp_player_construct),
        sizeof(char)
    },
    {
        "comp_player_creatrsonly",
        offsetof(struct Game, comp_player_creatrsonly),
        sizeof(char)
    },
    
    // Creature tendency settings (frontmenu_ingame_tabs_data.cpp)
    {
        "creatures_tend_imprison",
        offsetof(struct Game, creatures_tend_imprison),
        sizeof(TbBool)
    },
    {
        "creatures_tend_flee",
        offsetof(struct Game, creatures_tend_flee),
        sizeof(TbBool)
    },
    
    // Event text scroll window (frontmenu_ingame_evnt_data.cpp)
    {
        "evntbox_scroll_window",
        offsetof(struct Game, evntbox_scroll_window),
        sizeof(struct TextScrollWindow)
    },
    
    // Terminator
    {NULL, 0, 0}
};

/******************************************************************************/

void* game_resolve_field(const char *field_name)
{
    if (!field_name) {
        return NULL;
    }
    
    // Linear search through field definitions
    for (const GameFieldDef *def = game_field_defs; def->name != NULL; def++) {
        if (strcmp(def->name, field_name) == 0) {
            // Return address of field within Game struct
            return (void*)((char*)gpGame + def->offset);
        }
    }
    
    // Field not found in registry
    ERRORDBG(5, "Unknown field '%s'", field_name);
    return NULL;
}

const GameFieldDef* game_get_field_defs(void)
{
    return game_field_defs;
}

/******************************************************************************/
