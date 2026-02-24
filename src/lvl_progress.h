/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_progress.h
 *     Header file for lvl_progress.c.
 * @par Purpose:
 *     Per-campaign level completion tracking via progress.json.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef LVL_PROGRESS_H
#define LVL_PROGRESS_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROGRESS_ENTRIES 512

struct LevelProgress {
    LevelNumber lvnum;
    unsigned long best_score;
    TbBool completed;
};

struct CampaignProgress {
    struct LevelProgress entries[MAX_PROGRESS_ENTRIES];
    int count;
    TbBool loaded;
};

extern struct CampaignProgress campaign_progress;

TbBool load_progress(void);
TbBool save_progress(void);
TbBool record_level_completion(LevelNumber lvnum, unsigned long score);
TbBool is_level_completed(LevelNumber lvnum);
unsigned long get_level_best_score(LevelNumber lvnum);

#ifdef __cplusplus
}
#endif
#endif
