/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file lvl_progress.c
 *     Per-campaign level completion tracking via progress.json.
 * @par Purpose:
 *     Records which levels have been completed and the best score per level.
 *     Stored as saves/<campaign>/progress.json using CentiJSON for reading
 *     and fprintf for writing.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "lvl_progress.h"
#include "globals.h"
#include "game_saves.h"
#include "highscores.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"

#include <json.h>
#include <json-dom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "post_inc.h"

/******************************************************************************/
struct CampaignProgress campaign_progress;
static const char progress_filename[] = "progress.json";
/******************************************************************************/

static struct LevelProgress* find_progress_entry(LevelNumber lvnum)
{
    for (int i = 0; i < campaign_progress.count; i++)
    {
        if (campaign_progress.entries[i].lvnum == lvnum)
            return &campaign_progress.entries[i];
    }
    return NULL;
}

static struct LevelProgress* get_or_create_entry(LevelNumber lvnum)
{
    struct LevelProgress* entry = find_progress_entry(lvnum);
    if (entry != NULL)
        return entry;
    if (campaign_progress.count >= MAX_PROGRESS_ENTRIES)
    {
        WARNLOG("Progress entries full, cannot track level %d", (int)lvnum);
        return NULL;
    }
    entry = &campaign_progress.entries[campaign_progress.count];
    memset(entry, 0, sizeof(*entry));
    entry->lvnum = lvnum;
    campaign_progress.count++;
    return entry;
}

TbBool load_progress(void)
{
    memset(&campaign_progress, 0, sizeof(campaign_progress));
    campaign_progress.loaded = false;

    char *fpath = prepare_campaign_save_path(progress_filename);
    if (!LbFileExists(fpath))
    {
        campaign_progress.loaded = true;
        return true;
    }

    FILE *fp = fopen(fpath, "r");
    if (fp == NULL)
    {
        campaign_progress.loaded = true;
        return true;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0)
    {
        fclose(fp);
        campaign_progress.loaded = true;
        return true;
    }

    char *buf = (char *)calloc(1, fsize + 1);
    if (buf == NULL)
    {
        ERRORLOG("Cannot allocate %ld bytes for progress file", fsize);
        fclose(fp);
        return false;
    }

    size_t nread = fread(buf, 1, fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    // Parse JSON
    VALUE root;
    JSON_INPUT_POS pos;
    int ret = json_dom_parse(buf, nread, NULL, 0, &root, &pos);
    free(buf);

    if (ret != 0)
    {
        ERRORLOG("Failed to parse progress.json (err %d)", ret);
        value_fini(&root);
        return false;
    }

    VALUE *levels = value_dict_get(&root, "levels");
    if (levels != NULL && value_type(levels) == VALUE_DICT)
    {
        size_t nkeys = value_dict_size(levels);
        const VALUE **keys = (const VALUE **)calloc(nkeys, sizeof(VALUE*));
        if (keys != NULL)
        {
            value_dict_keys_sorted(levels, keys, nkeys);
            for (size_t ki = 0; ki < nkeys; ki++)
            {
                const char *key = value_string(keys[ki]);
                if (key == NULL)
                    continue;

                LevelNumber lvnum = (LevelNumber)atoi(key);
                if (lvnum <= 0)
                    continue;

                VALUE *entry_obj = value_dict_get(levels, key);
                if (entry_obj == NULL || value_type(entry_obj) != VALUE_DICT)
                    continue;

                struct LevelProgress *entry = get_or_create_entry(lvnum);
                if (entry == NULL)
                    break;

                VALUE *v_completed = value_dict_get(entry_obj, "completed");
                if (v_completed != NULL && value_type(v_completed) == VALUE_BOOL)
                    entry->completed = value_bool(v_completed) ? true : false;

                VALUE *v_score = value_dict_get(entry_obj, "best_score");
                if (v_score != NULL && value_is_compatible(v_score, VALUE_UINT32))
                    entry->best_score = value_uint32(v_score);
                else if (v_score != NULL && value_is_compatible(v_score, VALUE_INT32))
                    entry->best_score = (unsigned long)value_int32(v_score);
            }
            free(keys);
        }
    }

    value_fini(&root);
    campaign_progress.loaded = true;
    SYNCLOG("Loaded progress for %d levels", campaign_progress.count);
    return true;
}

TbBool save_progress(void)
{
    char *fpath = prepare_campaign_save_path(progress_filename);

    FILE *fp = fopen(fpath, "w");
    if (fp == NULL)
    {
        ERRORLOG("Cannot write progress file %s", fpath);
        return false;
    }

    fprintf(fp, "{\n  \"levels\": {\n");
    for (int i = 0; i < campaign_progress.count; i++)
    {
        struct LevelProgress *entry = &campaign_progress.entries[i];
        fprintf(fp, "    \"%d\": {\n", (int)entry->lvnum);
        fprintf(fp, "      \"completed\": %s,\n", entry->completed ? "true" : "false");
        fprintf(fp, "      \"best_score\": %lu\n", entry->best_score);
        fprintf(fp, "    }%s\n", (i < campaign_progress.count - 1) ? "," : "");
    }
    fprintf(fp, "  }\n}\n");
    fclose(fp);

    SYNCLOG("Saved progress for %d levels", campaign_progress.count);
    return true;
}

TbBool record_level_completion(LevelNumber lvnum, unsigned long score)
{
    if (!campaign_progress.loaded)
        load_progress();

    struct LevelProgress *entry = get_or_create_entry(lvnum);
    if (entry == NULL)
        return false;

    entry->completed = true;
    if (score > entry->best_score)
        entry->best_score = score;

    return save_progress();
}

TbBool is_level_completed(LevelNumber lvnum)
{
    if (!campaign_progress.loaded)
        load_progress();

    struct LevelProgress *entry = find_progress_entry(lvnum);
    if (entry != NULL)
        return entry->completed;

    // Fall back to high score table for legacy saves
    return (get_level_highest_score(lvnum) > 0);
}

unsigned long get_level_best_score(LevelNumber lvnum)
{
    if (!campaign_progress.loaded)
        load_progress();

    struct LevelProgress *entry = find_progress_entry(lvnum);
    if (entry != NULL)
        return entry->best_score;
    return 0;
}
