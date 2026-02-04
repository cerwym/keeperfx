/******************************************************************************/
/** @file achievement_definitions.h
 *     Header file for achievement_definitions.c.
 * @par Purpose:
 *     Achievement definition and configuration parsing.
 * @par Comment:
 *     Handles loading achievement definitions from campaign files.
 * @author   Peter Lockett & KeeperFX Team
 * @date     02 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef ACHIEVEMENT_DEFINITIONS_H
#define ACHIEVEMENT_DEFINITIONS_H

#include "../../bflib_basics.h"
#include "../../config.h"
#include "../../config_campaigns.h"
#include "achievement_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define MAX_ACHIEVEMENT_CONDITIONS 16

/** Achievement condition types */
enum AchievementConditionType {
    AchCond_None = 0,
    /** @name Level conditions */
    /**@{*/
    AchCond_Level,              ///< Specific level
    AchCond_LevelRange,         ///< Level range
    AchCond_CompleteLevel,      ///< Must complete level
    AchCond_LevelTimeUnder,     ///< Complete in time limit
    /**@}*/
    /** @name Creature conditions */
    /**@{*/
    AchCond_OnlyCreature,       ///< Only allow specific creature
    AchCond_MinCreatures,       ///< Minimum creatures of type
    AchCond_MaxCreatures,       ///< Maximum creatures of type
    AchCond_CreatureKill,       ///< Must kill specific creature
    AchCond_CreatureUsed,       ///< Must have specific creature
    AchCond_NoCreatureDeaths,   ///< No friendly casualties
    /**@}*/
    /** @name Combat conditions */
    /**@{*/
    AchCond_MaxSlaps,           ///< Maximum slaps allowed
    AchCond_MinKills,           ///< Minimum enemy kills
    AchCond_BattlesWon,         ///< Win battles
    AchCond_HeartsDestroyed,    ///< Destroy enemy hearts
    /**@}*/
    /** @name Resource conditions */
    /**@{*/
    AchCond_MaxGoldSpent,       ///< Budget limit
    AchCond_MinGold,            ///< Maintain gold level
    AchCond_TerritorySize,      ///< Control territory
    /**@}*/
    /** @name Dungeon conditions */
    /**@{*/
    AchCond_RoomRequired,       ///< Must have room type
    AchCond_RoomForbidden,      ///< Cannot build room type
    AchCond_SpellForbidden,     ///< Cannot use spell
    AchCond_TrapUsed,           ///< Must use trap type
    /**@}*/
    /** @name Custom conditions */
    /**@{*/
    AchCond_ScriptFlag,         ///< Custom flag set by script
    AchCond_LuaCondition,       ///< Lua callback
    /**@}*/
};

/** Achievement condition */
struct AchievementCondition {
    enum AchievementConditionType type;
    union {
        struct {
            int level_num;
        } level;
        struct {
            int min_level;
            int max_level;
        } level_range;
        struct {
            int time_seconds;
        } time_limit;
        struct {
            int creature_model;
        } creature;
        struct {
            int creature_model;
            int count;
        } creature_count;
        struct {
            int max_count;
        } slaps;
        struct {
            int min_count;
        } kills;
        struct {
            int count;
        } battles;
        struct {
            int count;
        } hearts;
        struct {
            long amount;
        } gold;
        struct {
            int tiles;
        } territory;
        struct {
            int room_kind;
        } room;
        struct {
            int spell_kind;
        } spell;
        struct {
            int trap_kind;
        } trap;
        struct {
            int flag_id;
        } script_flag;
        struct {
            char function_name[64];
        } lua;
    } data;
};

/** Achievement definition (template) */
struct AchievementDefinition {
    char id[ACHIEVEMENT_ID_LEN];
    char name[ACHIEVEMENT_NAME_LEN];
    char description[ACHIEVEMENT_DESC_LEN];
    char icon_path[ACHIEVEMENT_ICON_PATH_LEN];
    int points;
    int icon_sprite;  // Custom sprite index for achievement icon (0 = use default trophy icon)
    TbBool hidden;
    int name_text_id;   ///< String ID for localized name
    int desc_text_id;   ///< String ID for localized description
    char campaign_name[64];  ///< Campaign this achievement belongs to (for namespaced ID)
    
    /** @name Conditions that must be met */
    /**@{*/
    struct AchievementCondition conditions[MAX_ACHIEVEMENT_CONDITIONS];
    int condition_count;
    /**@}*/
};

/******************************************************************************/
/// Global achievement definitions array
extern struct AchievementDefinition achievement_definitions[MAX_ACHIEVEMENTS_PER_CAMPAIGN];
extern int achievement_definitions_count;

/******************************************************************************/
/**
 * Load achievement definitions from campaign.
 * @param campaign Campaign to load achievements from.
 * @return Number of achievements loaded.
 */
int load_campaign_achievements(struct GameCampaign* campaign);

/**
 * Load achievements from a specific file.
 * @param fname File name/path.
 * @param campaign Campaign context (for namespacing).
 * @return Number of achievements loaded.
 */
int load_achievements_config(const char* fname, struct GameCampaign* campaign);

/**
 * Parse achievement definition from config.
 * @param buf Config buffer.
 * @param campaign Campaign context.
 * @param achievement_def Output achievement definition.
 * @return True if successfully parsed.
 */
TbBool parse_achievement_definition(char* buf, struct GameCampaign* campaign, 
                                   struct AchievementDefinition* achievement_def);

/**
 * Check if achievement conditions are met.
 * @param achievement_def Achievement definition.
 * @return True if all conditions are met.
 */
TbBool check_achievement_conditions(struct AchievementDefinition* achievement_def);

/**
 * Register achievement definition.
 * @param achievement_def Achievement definition to register.
 * @param campaign_name Campaign name for namespacing.
 * @return True if successfully registered.
 */
TbBool register_achievement_definition(struct AchievementDefinition* achievement_def,
                                      const char* campaign_name);

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // ACHIEVEMENT_DEFINITIONS_H