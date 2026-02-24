/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file achievement_save.h
 *     Header file for achievement_save.c.
 * @par Purpose:
 *     Persistent achievement unlock state via achievements.json.
 *     Achievement state is stored separately from save files so that
 *     unlocks are permanent and immune to save struct changes.
 * @author   Peter Lockett, KeeperFX Team
 * @date     24 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef ACHIEVEMENT_SAVE_H
#define ACHIEVEMENT_SAVE_H

#include "../../bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Load achievement unlock state from saves/<campaign>/achievements.json.
 * Populates the unlocked/unlock_time/progress fields of the in-memory
 * achievements[] array. Safe to call when no file exists (treats as
 * all-locked). Must be called after load_campaign_achievements().
 * @return True on success (including "no file yet").
 */
TbBool load_achievement_state(void);

/**
 * Save current achievement unlock state to saves/<campaign>/achievements.json.
 * Writes immediately — called from achievement_unlock() so that unlocks
 * persist even if the game crashes before the next save.
 * Only writes achievements that have progress > 0 or are unlocked.
 * @return True on success.
 */
TbBool save_achievement_state(void);

#ifdef __cplusplus
}
#endif
#endif // ACHIEVEMENT_SAVE_H
