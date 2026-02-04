/******************************************************************************/
/** @file achievement_definitions.c
 *     Achievement definition and configuration parsing.
 * @par Purpose:
 *     Handles loading and parsing achievement definitions from campaign files.
 * @par Comment:
 *     Implements the achievement definition system.
 * @author   Peter Lockett & KeeperFX Team
 * @date     02 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "achievement_definitions.h"

#include <string.h>
#include <stdio.h>
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../bflib_string.h"
#include "../../config.h"
#include "../../config_strings.h"
#include "../../game_legacy.h"
#include "../../keeperfx.hpp"
#include "../../post_inc.h"

/******************************************************************************/
/** Config commands for achievement parsing */
const struct NamedCommand achievement_commands[] = {
    {"ACHIEVEMENT",         1},
    {"END_ACHIEVEMENT",     2},
    {"NAME_TEXT_ID",        3},
    {"DESC_TEXT_ID",        4},
    {"ICON",                5},
    {"HIDDEN",              6},
    {"POINTS",              7},
    {"LEVEL",               8},
    {"LEVEL_RANGE",         9},
    {"COMPLETE_LEVEL",     10},
    {"LEVEL_TIME_UNDER",   11},
    {"ONLY_CREATURE",      12},
    {"MIN_CREATURES",      13},
    {"MAX_CREATURES",      14},
    {"CREATURE_KILL",      15},
    {"CREATURE_USED",      16},
    {"NO_CREATURE_DEATHS", 17},
    {"MAX_SLAPS",          18},
    {"MIN_KILLS",          19},
    {"BATTLES_WON",        20},
    {"HEARTS_DESTROYED",   21},
    {"MAX_GOLD_SPENT",     22},
    {"MIN_GOLD",           23},
    {"TERRITORY_SIZE",     24},
    {"ROOM_REQUIRED",      25},
    {"ROOM_FORBIDDEN",     26},
    {"SPELL_FORBIDDEN",    27},
    {"TRAP_USED",          28},
    {"SCRIPT_FLAG",        29},
    {"LUA_CONDITION",      30},
    {NULL,                  0},
};

/******************************************************************************/
/**
 * Load achievements for a campaign.
 * Searches for achievements.cfg in campaign directories.
 * @param campaign The campaign to load achievements for.
 * @return Number of achievements loaded.
 */
int load_campaign_achievements(struct GameCampaign* campaign)
{
    if (campaign == NULL)
    {
        ERRORLOG("Cannot load achievements for NULL campaign");
        return 0;
    }
    
    char fname[DISKPATH_SIZE];
    
    // Try loading from campaign's configs location
    if (campaign->configs_location[0] != '\0')
    {
        LbStringCopy(fname, campaign->configs_location, DISKPATH_SIZE);
        LbStringCat(fname, "/achievements.cfg", DISKPATH_SIZE);
        
        if (LbFileExists(fname))
        {
            SYNCLOG("Loading achievements from: %s", fname);
            return load_achievements_config(fname, campaign);
        }
    }
    
    // Try loading from campaign's levels location
    if (campaign->levels_location[0] != '\0')
    {
        LbStringCopy(fname, campaign->levels_location, DISKPATH_SIZE);
        LbStringCat(fname, "/achievements.cfg", DISKPATH_SIZE);
        
        if (LbFileExists(fname))
        {
            SYNCLOG("Loading achievements from: %s", fname);
            return load_achievements_config(fname, campaign);
        }
    }
    
    // No achievements file found
    ("No achievements.cfg found for campaign '%s'", campaign->name);
    return 0;
}

/**
 * Load achievements configuration from file.
 * Parses achievement definitions from a config file.
 * @param fname Path to the achievements config file.
 * @param campaign The campaign these achievements belong to.
 * @return Number of achievements loaded.
 */
int load_achievements_config(const char* fname, struct GameCampaign* campaign)
{
    char* buf;
    long len;
    int loaded_count = 0;
    
    /// Load config file
    len = LbFileLengthRnc(fname);
    if (len <= 0)
    {
        ERRORLOG("Achievement config file empty or doesn't exist: %s", fname);
        return 0;
    }
    
    buf = (char*)LbMemoryAlloc(len + 256);
    if (buf == NULL)
    {
        ERRORLOG("Cannot allocate memory for achievement config");
        return 0;
    }
    
    len = LbFileLoadAt(fname, buf);
    if (len <= 0)
    {
        ERRORLOG("Cannot load achievement config file: %s", fname);
        LbMemoryFree(buf);
        return 0;
    }
    
    buf[len] = '\0';
    
    /// Parse achievements
    char* pos = buf;
    struct AchievementDefinition ach_def;
    TbBool in_achievement = false;
    
    while (*pos != '\0')
    {
        /// Skip whitespace and comments
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n' || *pos == ';')
        {
            if (*pos == ';')
            {
                /// Skip comment line
                while (*pos != '\n' && *pos != '\0')
                    pos++;
            }
            if (*pos != '\0')
                pos++;
        }
        
        if (*pos == '\0')
            break;
        
        /// Read command
        char cmd[COMMAND_WORD_LEN];
        int i = 0;
        while (*pos != ' ' && *pos != '\t' && *pos != '\n' && *pos != '\0' && i < COMMAND_WORD_LEN - 1)
        {
            cmd[i++] = *pos++;
        }
        cmd[i] = '\0';
        
        /// Find command
        int cmd_num = 0;
        for (const struct NamedCommand* ncmd = achievement_commands; ncmd->name != NULL; ncmd++)
        {
            if (strcasecmp(cmd, ncmd->name) == 0)
            {
                cmd_num = ncmd->num;
                break;
            }
        }
        
        /// Process command
        if (cmd_num == 1) ///< ACHIEVEMENT
        {
            if (in_achievement)
            {
                WARNLOG("Nested ACHIEVEMENT command found");
            }
            
            /// Initialize new achievement definition
            memset(&ach_def, 0, sizeof(ach_def));
            in_achievement = true;
            
            /// Read achievement ID
            while (*pos == ' ' || *pos == '\t')
                pos++;
            i = 0;
            while (*pos != ' ' && *pos != '\t' && *pos != '\n' && *pos != '\0' && i < ACHIEVEMENT_ID_LEN - 1)
            {
                ach_def.id[i++] = *pos++;
            }
            ach_def.id[i] = '\0';
        }
        else if (cmd_num == 2) ///< END_ACHIEVEMENT
        {
            if (!in_achievement)
            {
                WARNLOG("END_ACHIEVEMENT without ACHIEVEMENT");
                continue;
            }
            
            /// Register achievement
            if (register_achievement_definition(&ach_def, campaign->name))
            {
                loaded_count++;
            }
            
            in_achievement = false;
        }
        else if (in_achievement)
        {
            /// Parse achievement properties
            /// @note Simplified parsing - full implementation would handle all condition types
            switch(cmd_num)
            {
                case 3: ///< NAME_TEXT_ID
                    /// Read integer
                    while (*pos == ' ' || *pos == '\t') pos++;
                    sscanf(pos, "%d", &ach_def.name_text_id);
                    break;
                    
                case 4: ///< DESC_TEXT_ID
                    while (*pos == ' ' || *pos == '\t') pos++;
                    sscanf(pos, "%d", &ach_def.desc_text_id);
                    break;
                    
                case 7: ///< POINTS
                    while (*pos == ' ' || *pos == '\t') pos++;
                    sscanf(pos, "%d", &ach_def.points);
                    break;
                    
                case 6: ///< HIDDEN
                    while (*pos == ' ' || *pos == '\t') pos++;
                    sscanf(pos, "%d", (int*)&ach_def.hidden);
                    break;
                    
                /// Additional condition parsing would go here
            }
        }
        
        /// Skip to next line
        while (*pos != '\n' && *pos != '\0')
            pos++;
    }
    
    LbMemoryFree(buf);
    
    SYNCLOG("Loaded %d achievements for campaign '%s'", loaded_count, campaign->name);
    return loaded_count;
}

/**
 * Register an achievement definition.
 * Creates and registers an achievement from a definition.
 * @param achievement_def The achievement definition to register.
 * @param campaign_name The name of the campaign this achievement belongs to.
 * @return True if successfully registered.
 */
TbBool register_achievement_definition(struct AchievementDefinition* achievement_def,
                                      const char* campaign_name)
{
    if (achievement_def == NULL || campaign_name == NULL)
        return false;
    
    /// Create namespaced achievement
    struct Achievement ach;
    memset(&ach, 0, sizeof(ach));
    
    /// Namespace achievement ID with campaign name
    snprintf(ach.id, ACHIEVEMENT_ID_LEN, "%s.%s", campaign_name, achievement_def->id);
    
    /// Copy basic properties
    if (achievement_def->name_text_id > 0)
    {
        /// Use localized string
        const char* str = get_string(achievement_def->name_text_id);
        if (str != NULL)
        {
            LbStringCopy(ach.name, str, ACHIEVEMENT_NAME_LEN);
        }
    }
    else
    {
        LbStringCopy(ach.name, achievement_def->name, ACHIEVEMENT_NAME_LEN);
    }
    
    if (achievement_def->desc_text_id > 0)
    {
        const char* str = get_string(achievement_def->desc_text_id);
        if (str != NULL)
        {
            LbStringCopy(ach.description, str, ACHIEVEMENT_DESC_LEN);
        }
    }
    else
    {
        LbStringCopy(ach.description, achievement_def->description, ACHIEVEMENT_DESC_LEN);
    }
    
    LbStringCopy(ach.icon_path, achievement_def->icon_path, ACHIEVEMENT_ICON_PATH_LEN);
    ach.points = achievement_def->points;
    ach.hidden = achievement_def->hidden;
    ach.unlocked = false;
    ach.unlock_time = 0;
    ach.progress = 0.0f;
    
    /// Register with achievement system
    return achievement_register(&ach);
}

/**
 * Check if achievement conditions are met.
 * @param achievement_def The achievement definition to check.
 * @return True if all conditions are satisfied.
 * @todo Implement condition checking logic.
 * @note This will check game state against achievement conditions.
 * @note Currently not implemented, returns false.
 */
TbBool check_achievement_conditions(struct AchievementDefinition* achievement_def)
{
    return false;
}

/******************************************************************************/