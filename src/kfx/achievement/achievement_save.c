/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file achievement_save.c
 *     Persistent achievement unlock state via achievements.json.
 * @par Purpose:
 *     Reads and writes achievement unlock state to JSON files:
 *       - save/<campaign>/achievements.json for campaign-scoped achievements
 *       - save/achievements_global.json for global (cross-campaign) achievements
 *     Each file includes a CRC32 checksum to deter casual tampering.
 *     This is separate from save game files so that achievement unlocks are
 *     permanent and not affected by save struct changes.
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
#include "../../config.h"
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"

#include <json.h>
#include <json-dom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../../post_inc.h"

/******************************************************************************/
static const char achievements_filename[] = "achievements.json";
static const char global_achievements_filename[] = "achievements_global.json";
/// Salt XOR'd with CRC32 to deter trivial recomputation
static const unsigned long CHECKSUM_SALT = 0x4B465841UL; // "KFXA"
/******************************************************************************/

/**
 * Compute a salted CRC32 checksum over achievement data for tamper detection.
 * Iterates the given achievement array and checksums the id, unlocked,
 * unlock_time, and progress fields of entries that have state worth saving.
 */
static unsigned long compute_achievement_checksum(TbBool global_scope)
{
    unsigned long crc = crc32(0L, Z_NULL, 0);

    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        TbBool is_global = (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                                    sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0);
        if (is_global != global_scope)
            continue;
        if (!ach->unlocked && ach->progress <= 0.0f)
            continue;

        crc = crc32(crc, (const Bytef *)ach->id, (uInt)strlen(ach->id));
        unsigned char u = ach->unlocked ? 1 : 0;
        crc = crc32(crc, &u, 1);
        crc = crc32(crc, (const Bytef *)&ach->unlock_time, sizeof(ach->unlock_time));
        crc = crc32(crc, (const Bytef *)&ach->progress, sizeof(ach->progress));
    }

    return crc ^ CHECKSUM_SALT;
}

/**
 * Internal: load achievement state from a JSON file at the given path.
 * Verifies checksum if present; discards tampered files.
 * @param fpath Full path to the JSON file.
 * @param label Human-readable label for log messages.
 * @return True on success.
 */
static TbBool load_achievement_state_from(const char *fpath, const char *label,
                                          TbBool global_scope)
{
    if (!LbFileExists(fpath))
    {
        SYNCDBG(7, "No %s found, starting fresh", label);
        return true;
    }

    FILE *fp = fopen(fpath, "r");
    if (fp == NULL)
    {
        SYNCDBG(7, "Cannot open %s, starting fresh", label);
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
        ERRORLOG("Cannot allocate %ld bytes for %s", fsize, label);
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
        ERRORLOG("Failed to parse %s (err %d)", label, ret);
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
                    continue;

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
                    if (value_is_compatible(v_progress, VALUE_DOUBLE))
                        ach->progress = (float)value_double(v_progress);
                    else if (value_is_compatible(v_progress, VALUE_INT32))
                        ach->progress = (float)value_int32(v_progress);
                }
            }
            free(keys);
        }
    }

    // Verify checksum if present
    VALUE *v_checksum = value_dict_get(&root, "checksum");
    if (v_checksum != NULL && value_type(v_checksum) == VALUE_STRING)
    {
        unsigned long stored = strtoul(value_string(v_checksum), NULL, 16);
        unsigned long computed = compute_achievement_checksum(global_scope);
        if (stored != computed)
        {
            WARNLOG("Checksum mismatch in %s (stored %08lx, computed %08lx) — "
                    "file may have been tampered with, resetting",
                    label, stored, computed);
            // Reset all achievements loaded from this file
            for (int i = 0; i < achievements_count; i++)
            {
                struct Achievement *ach = &achievements[i];
                TbBool is_global = (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                                            sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0);
                if (is_global != global_scope)
                    continue;
                ach->unlocked = false;
                ach->unlock_time = 0;
                ach->progress = 0.0f;
            }
            value_fini(&root);
            return true;
        }
    }

    value_fini(&root);
    SYNCLOG("Loaded %s", label);
    return true;
}

/**
 * Internal: save achievement state to a JSON file.
 * Writes only achievements matching the given scope and includes a CRC32 checksum.
 * @param fpath Full path to write.
 * @param label Human-readable label for log messages.
 * @param global_scope If true, write only global achievements; otherwise campaign only.
 * @return True on success.
 */
static TbBool save_achievement_state_to(const char *fpath, const char *label,
                                        TbBool global_scope)
{
    FILE *fp = fopen(fpath, "w");
    if (fp == NULL)
    {
        ERRORLOG("Cannot write %s: %s", label, fpath);
        return false;
    }

    unsigned long checksum = compute_achievement_checksum(global_scope);

    fprintf(fp, "{\n  \"version\": 1,\n  \"checksum\": \"%08lx\",\n  \"achievements\": {\n",
            checksum);

    int written = 0;
    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        TbBool is_global = (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                                    sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0);
        if (is_global != global_scope)
            continue;
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

    SYNCDBG(8, "Saved %s (%d entries)", label, written);
    return true;
}

/******************************************************************************/

TbBool load_achievement_state(void)
{
    char *fpath = prepare_campaign_save_path(achievements_filename);
    return load_achievement_state_from(fpath, "campaign achievements", false);
}

TbBool save_achievement_state(void)
{
    char *fpath = prepare_campaign_save_path(achievements_filename);
    return save_achievement_state_to(fpath, "campaign achievements", false);
}

TbBool load_achievement_state_global(void)
{
    char *fpath = prepare_file_path(FGrp_Save, global_achievements_filename);
    return load_achievement_state_from(fpath, "global achievements", true);
}

TbBool save_achievement_state_global(void)
{
    char *fpath = prepare_file_path(FGrp_Save, global_achievements_filename);
    return save_achievement_state_to(fpath, "global achievements", true);
}

TbBool delete_achievement_state(void)
{
    char *fpath = prepare_campaign_save_path(achievements_filename);
    if (LbFileExists(fpath))
    {
        LbFileDelete(fpath);
        SYNCLOG("Deleted campaign achievement save: %s", fpath);
    }
    return true;
}

TbBool delete_achievement_state_global(void)
{
    char *fpath = prepare_file_path(FGrp_Save, global_achievements_filename);
    if (LbFileExists(fpath))
    {
        LbFileDelete(fpath);
        SYNCLOG("Deleted global achievement save: %s", fpath);
    }
    return true;
}
