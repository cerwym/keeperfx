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
#include <stdlib.h>
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../bflib_string.h"
#include "../../bflib_dernc.h"
#include "../../config.h"
#include "../../config_strings.h"
#include "../../game_legacy.h"
#include "../../keeperfx.hpp"
#include "../../post_inc.h"

/******************************************************************************/
/// Global array of achievement definitions
struct AchievementDefinition achievement_definitions[MAX_ACHIEVEMENTS_PER_CAMPAIGN];
int achievement_definitions_count = 0;

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
int load_campaign_achievements(struct GameCampaign* camp)
{
    if (camp == NULL)
    {
        ERRORLOG("Cannot load achievements for NULL campaign");
        return 0;
    }
    
    char fname[DISKPATH_SIZE];
    
    // Try loading from campaign's configs location
    if (camp->configs_location[0] != '\0')
    {
        snprintf(fname, DISKPATH_SIZE, "%s/achievements.cfg", camp->configs_location);
        
        if (LbFileExists(fname))
        {
            SYNCLOG("Loading achievements from: %s", fname);
            return load_achievements_config(fname, camp);
        }
    }
    
    // Try loading from campaign's levels location
    if (camp->levels_location[0] != '\0')
    {
        snprintf(fname, DISKPATH_SIZE, "%s/achievements.cfg", camp->levels_location);
        
        if (LbFileExists(fname))
        {
            SYNCLOG("Loading achievements from: %s", fname);
            return load_achievements_config(fname, camp);
        }
    }
    
    // No achievements file found
    SYNCDBG(7, "No achievements.cfg found for campaign '%s'", camp->name);
    return 0;
}

/**
 * Load achievements configuration from file.
 * Parses achievement definitions from a config file.
 * @param fname Path to the achievements config file.
 * @param camp The campaign these achievements belong to.
 * @return Number of achievements loaded.
 */
int load_achievements_config(const char* fname, struct GameCampaign* camp)
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
    
    buf = (char*)calloc(len + 256, 1);
    if (buf == NULL)
    {
        ERRORLOG("Cannot allocate memory for achievement config");
        return 0;
    }
    
    len = LbFileLoadAt(fname, buf);
    if (len <= 0)
    {
        ERRORLOG("Cannot load achievement config file: %s", fname);
        free(buf);
        return 0;
    }
    
    buf[len] = '\0';
    
    /// Parse achievements
    char* pos = buf;
    struct AchievementDefinition ach_def;
    TbBool in_achievement = false;
    
    while (*pos != '\0')
    {
        /// Skip whitespace
        while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n')
        {
            pos++;
        }
        
        /// Skip comment lines
        if (*pos == ';')
        {
            /// Skip entire comment line including newline
            while (*pos != '\n' && *pos != '\0')
                pos++;
            if (*pos == '\n')
                pos++;
            continue;
        }
        
        if (*pos == '\0')
            break;
        
        /// Read command
        char cmd[COMMAND_WORD_LEN];
        int i = 0;
        while (*pos != ' ' && *pos != '\t' && *pos != '\r' && *pos != '\n' && *pos != '\0' && i < COMMAND_WORD_LEN - 1)
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
                WARNLOG("Nested ACHIEVEMENT command found (previous ID: %s)", ach_def.id);
            }
            
            /// Initialize new achievement definition
            memset(&ach_def, 0, sizeof(ach_def));
            in_achievement = true;
            
            /// Read achievement ID
            while (*pos == ' ' || *pos == '\t')
                pos++;
            i = 0;
            while (*pos != ' ' && *pos != '\t' && *pos != '\r' && *pos != '\n' && *pos != '\0' && i < ACHIEVEMENT_ID_LEN - 1)
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
            if (register_achievement_definition(&ach_def, camp->name))
            {
                loaded_count++;
            }
            else
            {
                WARNLOG("Failed to register achievement '%s'", ach_def.id);
            }
            
            in_achievement = false;
        }
        else if (in_achievement)
        {
            /// Parse achievement properties and conditions
            struct AchievementCondition* cond;
            
            switch(cmd_num)
            {
                case 3: ///< NAME_TEXT_ID
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
                    
                /// Level conditions
                case 8: ///< LEVEL
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_Level;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.level.level_num);
                    }
                    break;
                    
                case 9: ///< LEVEL_RANGE
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_LevelRange;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d %d", &cond->data.level_range.min_level, 
                               &cond->data.level_range.max_level);
                    }
                    break;
                    
                case 10: ///< COMPLETE_LEVEL
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_CompleteLevel;
                    }
                    break;
                    
                case 11: ///< LEVEL_TIME_UNDER
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_LevelTimeUnder;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.time_limit.time_seconds);
                    }
                    break;
                    
                /// Creature conditions
                case 12: ///< ONLY_CREATURE
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_OnlyCreature;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.creature.creature_model);
                    }
                    break;
                    
                case 13: ///< MIN_CREATURES
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MinCreatures;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d %d", &cond->data.creature_count.creature_model,
                               &cond->data.creature_count.count);
                    }
                    break;
                    
                case 14: ///< MAX_CREATURES
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MaxCreatures;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d %d", &cond->data.creature_count.creature_model,
                               &cond->data.creature_count.count);
                    }
                    break;
                    
                case 15: ///< CREATURE_KILL
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_CreatureKill;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.creature.creature_model);
                    }
                    break;
                    
                case 16: ///< CREATURE_USED
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_CreatureUsed;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.creature.creature_model);
                    }
                    break;
                    
                case 17: ///< NO_CREATURE_DEATHS
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_NoCreatureDeaths;
                    }
                    break;
                    
                /// Combat conditions
                case 18: ///< MAX_SLAPS
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MaxSlaps;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.slaps.max_count);
                    }
                    break;
                    
                case 19: ///< MIN_KILLS
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MinKills;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.kills.min_count);
                    }
                    break;
                    
                case 20: ///< BATTLES_WON
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_BattlesWon;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.battles.count);
                    }
                    break;
                    
                case 21: ///< HEARTS_DESTROYED
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_HeartsDestroyed;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.hearts.count);
                    }
                    break;
                    
                /// Resource conditions
                case 22: ///< MAX_GOLD_SPENT
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MaxGoldSpent;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%ld", &cond->data.gold.amount);
                    }
                    break;
                    
                case 23: ///< MIN_GOLD
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_MinGold;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%ld", &cond->data.gold.amount);
                    }
                    break;
                    
                case 24: ///< TERRITORY_SIZE
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_TerritorySize;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.territory.tiles);
                    }
                    break;
                    
                /// Dungeon conditions
                case 25: ///< ROOM_REQUIRED
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_RoomRequired;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.room.room_kind);
                    }
                    break;
                    
                case 26: ///< ROOM_FORBIDDEN
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_RoomForbidden;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.room.room_kind);
                    }
                    break;
                    
                case 27: ///< SPELL_FORBIDDEN
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_SpellForbidden;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.spell.spell_kind);
                    }
                    break;
                    
                case 28: ///< TRAP_USED
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_TrapUsed;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.trap.trap_kind);
                    }
                    break;
                    
                /// Custom conditions
                case 29: ///< SCRIPT_FLAG
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_ScriptFlag;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        sscanf(pos, "%d", &cond->data.script_flag.flag_id);
                    }
                    break;
                    
                case 30: ///< LUA_CONDITION
                    if (ach_def.condition_count < MAX_ACHIEVEMENT_CONDITIONS)
                    {
                        cond = &ach_def.conditions[ach_def.condition_count++];
                        cond->type = AchCond_LuaCondition;
                        while (*pos == ' ' || *pos == '\t') pos++;
                        i = 0;
                        while (*pos != ' ' && *pos != '\t' && *pos != '\r' && *pos != '\n' && *pos != '\0' && i < 63)
                        {
                            cond->data.lua.function_name[i++] = *pos++;
                        }
                        cond->data.lua.function_name[i] = '\0';
                    }
                    break;
                    
                default:
                    if (cmd_num != 0)
                    {
                        WARNLOG("Unknown achievement command: %s", cmd);
                    }
                    break;
            }
        }
        
        /// Skip to next line
        while (*pos != '\n' && *pos != '\0')
            pos++;
    }
    
    free(buf);
    
    SYNCLOG("Loaded %d achievements for campaign '%s'", loaded_count, camp->name);
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
    
    /// Store the definition for condition checking
    if (achievement_definitions_count >= MAX_ACHIEVEMENTS_PER_CAMPAIGN)
    {
        ERRORLOG("Achievement definitions limit reached");
        return false;
    }
    
    /// Copy definition to global array and store campaign name
    memcpy(&achievement_definitions[achievement_definitions_count], achievement_def, 
           sizeof(struct AchievementDefinition));
    strncpy(achievement_definitions[achievement_definitions_count].campaign_name, 
            campaign_name, sizeof(achievement_definitions[0].campaign_name) - 1);
    achievement_definitions[achievement_definitions_count].campaign_name[
        sizeof(achievement_definitions[0].campaign_name) - 1] = '\0';
    achievement_definitions_count++;
    
    /// Create namespaced achievement for API layer
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
            strncpy(ach.name, str, ACHIEVEMENT_NAME_LEN - 1);
            ach.name[ACHIEVEMENT_NAME_LEN - 1] = '\0';
        }
    }
    else
    {
        strncpy(ach.name, achievement_def->name, ACHIEVEMENT_NAME_LEN - 1);
        ach.name[ACHIEVEMENT_NAME_LEN - 1] = '\0';
    }
    
    if (achievement_def->desc_text_id > 0)
    {
        const char* str = get_string(achievement_def->desc_text_id);
        if (str != NULL)
        {
            strncpy(ach.description, str, ACHIEVEMENT_DESC_LEN - 1);
            ach.description[ACHIEVEMENT_DESC_LEN - 1] = '\0';
        }
    }
    else
    {
        strncpy(ach.description, achievement_def->description, ACHIEVEMENT_DESC_LEN - 1);
        ach.description[ACHIEVEMENT_DESC_LEN - 1] = '\0';
    }
    
    strncpy(ach.icon_path, achievement_def->icon_path, ACHIEVEMENT_ICON_PATH_LEN - 1);
    ach.icon_path[ACHIEVEMENT_ICON_PATH_LEN - 1] = '\0';
    ach.points = achievement_def->points;
    ach.hidden = achievement_def->hidden;
    ach.unlocked = false;
    ach.name_text_id = achievement_def->name_text_id;
    ach.desc_text_id = achievement_def->desc_text_id;
    ach.unlock_time = 0;
    ach.progress = 0.0f;
    
    /// Register with achievement system
    TbBool result = achievement_register(&ach);
    
    if (result)
    {
        SYNCLOG("Registered achievement: %s (%s)", ach.id, ach.name);
    }
    
    return result;
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