/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file game_field_lookup.h
 *     Header file for game_field_lookup.c
 * @par Purpose:
 *     Dynamic field resolution for Game struct. Provides string-based lookups
 *     to resolve button content pointers at runtime instead of compile time.
 *     This solves initialization ordering issues where GUI buttons are compiled
 *     with &game.field references before the Game struct is allocated.
 * @par Comment:
 *     Lazy initialization pattern: buttons store field names, pointers are
 *     resolved when game is initialized and menus are created.
 * @author   Automated refactoring
 * @date     2026
 */
/******************************************************************************/

#ifndef DK_GAME_FIELD_LOOKUP_H
#define DK_GAME_FIELD_LOOKUP_H

#include "bflib_basics.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/

/**
 * Definition of a Game struct field for runtime lookup
 */
typedef struct {
    const char *name;       /**< Field name string identifier */
    size_t offset;          /**< Offset of field within Game struct (from start) */
    size_t size;            /**< Size of field in bytes (for validation) */
} GameFieldDef;

/**
 * Resolve a Game struct field by name.
 * 
 * @param field_name Name of the field to look up (e.g., "comp_player_aggressive")
 * @return Pointer to the field within the Game struct, or NULL if not found or game not allocated
 * 
 * @par Safety:
 *     Returns NULL if gpGame is NULL or field not found. Caller should NULL-check.
 *     Field pointers are only valid while Game struct is allocated (normal gameplay).
 * 
 * @par Usage:
 *     void *ptr = game_resolve_field("comp_player_aggressive");
 *     if (ptr) { *(char*)ptr = new_value; }
 */
void* game_resolve_field(const char *field_name);

/**
 * List all available Game struct fields (for debugging/documentation)
 * 
 * @return Pointer to array of GameFieldDef, terminated by {NULL, 0, 0}
 */
const GameFieldDef* game_get_field_defs(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
