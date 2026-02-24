/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file achievement_save.c
 *     Persistent achievement unlock state via achievements.json.
 * @par Purpose:
 *     Reads and writes per-campaign achievement unlock state to a JSON file
 *     stored at saves/<campaign>/achievements.json. This is separate from
 *     save game files so that achievement unlocks are permanent and not
 *     affected by save struct changes, save deletion, or slot switching.
 *     Follows the same pattern as lvl_progress.c.
 * @author   Peter Lockett, KeeperFX Team
 * @date     24 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"

#include "achievement_save.h"
#include "achievement_api.h"
#include "../../game_saves.h"
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"

#include <json.h>
#include <json-dom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../post_inc.h"

/******************************************************************************/
static const char achievements_filename[] = "achievements.json";
/******************************************************************************/

TbBool load_achievement_state(void)
{
    char *fpath = prepare_campaign_save_path(achievements_filename);
    if (!LbFileExists(fpath))
    {
        SYNCDBG(7, "No achievements.json found, starting fresh");
        return true;
    }

    FILE *fp = fopen(fpath, "r");
    if (fp == NULL)
    {
        SYNCDBG(7, "Cannot open achievements.json, starting fresh");
        return true;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0)
    {
        fclose(fp);
        return true;
    }

    char *buf = (char *)calloc(1, fsize + 1);
    if (buf == NULL)
    {
        ERRORLOG("Cannot allocate %ld bytes for achievements file", fsize);
        fclose(fp);
        return false;
    }

    size_t nread = fread(buf, 1, fsize, fp);
    fclose(fp);
    buf[nread] = '\0';

    VALUE root;
    JSON_INPUT_POS pos;
    int ret = json_dom_parse(buf, nread, NULL, 0, &root, &pos);
    free(buf);

    if (ret != 0)
    {
        ERRORLOG("Failed to parse achievements.json (err %d)", ret);
        value_fini(&root);
        return false;
    }

    VALUE *achs = value_dict_get(&root, "achievements");
    if (achs != NULL && value_type(achs) == VALUE_DICT)
    {
        size_t nkeys = value_dict_size(achs);
        const VALUE **keys = (const VALUE **)calloc(nkeys, sizeof(VALUE *));
        if (keys != NULL)
        {
            value_dict_keys_sorted(achs, keys, nkeys);
            for (size_t ki = 0; ki < nkeys; ki++)
            {
                const char *ach_id = value_string(keys[ki]);
                if (ach_id == NULL)
                    continue;

                struct Achievement *ach = achievement_find(ach_id);
                if (ach == NULL)
                {
                    // Orphan entry — achievement removed from config, ignore
                    continue;
                }

                VALUE *entry_obj = value_dict_get(achs, ach_id);
                if (entry_obj == NULL || value_type(entry_obj) != VALUE_DICT)
                    continue;

                VALUE *v_unlocked = value_dict_get(entry_obj, "unlocked");
                if (v_unlocked != NULL && value_type(v_unlocked) == VALUE_BOOL)
                    ach->unlocked = value_bool(v_unlocked) ? true : false;

                VALUE *v_time = value_dict_get(entry_obj, "unlock_time");
                if (v_time != NULL && value_is_compatible(v_time, VALUE_INT64))
                    ach->unlock_time = (time_t)value_int64(v_time);
                else if (v_time != NULL && value_is_compatible(v_time, VALUE_UINT32))
                    ach->unlock_time = (time_t)value_uint32(v_time);

                VALUE *v_progress = value_dict_get(entry_obj, "progress");
                if (v_progress != NULL)
                {
                    // CentiJSON stores numbers; extract as double then cast
                    if (value_is_compatible(v_progress, VALUE_DOUBLE))
                        ach->progress = (float)value_double(v_progress);
                    else if (value_is_compatible(v_progress, VALUE_INT32))
                        ach->progress = (float)value_int32(v_progress);
                }
            }
            free(keys);
        }
    }

    value_fini(&root);
    int unlocked = achievements_get_unlocked_count();
    SYNCLOG("Loaded achievement state: %d/%d unlocked", unlocked, achievements_count);
    return true;
}

TbBool save_achievement_state(void)
{
    char *fpath = prepare_campaign_save_path(achievements_filename);

    FILE *fp = fopen(fpath, "w");
    if (fp == NULL)
    {
        ERRORLOG("Cannot write achievements file %s", fpath);
        return false;
    }

    fprintf(fp, "{\n  \"version\": 1,\n  \"achievements\": {\n");
    int written = 0;
    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        // Only persist achievements with state worth saving
        if (!ach->unlocked && ach->progress <= 0.0f)
            continue;

        if (written > 0)
            fprintf(fp, ",\n");

        fprintf(fp, "    \"%s\": {\n", ach->id);
        fprintf(fp, "      \"unlocked\": %s,\n", ach->unlocked ? "true" : "false");
        fprintf(fp, "      \"unlock_time\": %lld,\n", (long long)ach->unlock_time);
        fprintf(fp, "      \"progress\": %.4f\n", (double)ach->progress);
        fprintf(fp, "    }");
        written++;
    }

    if (written > 0)
        fprintf(fp, "\n");
    fprintf(fp, "  }\n}\n");
    fclose(fp);

    SYNCDBG(8, "Saved achievement state (%d entries)", written);
    return true;
}
