/******************************************************************************/
/** @file achievement_tracker.h
 *     Header file for achievement_tracker.c.
 * @par Purpose:
 *     Achievement tracking and condition checking.
 * @par Comment:
 *     Monitors game events and unlocks achievements when conditions are met.
 * @author   Peter Lockett & KeeperFX Team
 * @date     02 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef ACHIEVEMENT_TRACKER_H
#define ACHIEVEMENT_TRACKER_H

// Forward declarations
struct Thing;

#include "../../bflib_basics.h"
#include "achievement_api.h"
#include "achievement_definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/** Achievement tracking state for current level */
struct AchievementTracker {
    LevelNumber current_level;
    long level_start_time;        ///< Game turn when level started
    long creature_deaths;         ///< Friendly creature deaths
    long slaps_used;             ///< Number of slaps
    long enemy_kills;            ///< Enemy creatures killed
    long battles_won;            ///< Battles won
    long hearts_destroyed;       ///< Enemy hearts destroyed
    long gold_spent;             ///< Gold spent this level
    TbBool level_completed;      ///< Level won
    
    /** @name Creature tracking */
    /**@{*/
    int creature_types_used;     ///< Bitmask of creature types used
    int max_creatures[32];       ///< Max count of each creature type
    /**@}*/
    
    /** @name Dungeon tracking */
    /**@{*/
    int rooms_built;             ///< Bitmask of room types built
    int spells_used;             ///< Bitmask of spells used
    int traps_used;              ///< Bitmask of traps used
    /**@}*/
    
    /** @name Active achievements being tracked */
    /**@{*/
    struct AchievementDefinition* active_achievements[MAX_ACHIEVEMENTS_PER_CAMPAIGN];
    int active_achievement_count;
    /**@}*/
};

extern struct AchievementTracker achievement_tracker;

/******************************************************************************/
/**
 * Initialize achievement tracker for new level.
 * @param level_num Level number.
 */
void achievement_tracker_init(LevelNumber level_num);

/**
 * Reset tracker state (called on level start).
 */
void achievement_tracker_reset(void);

/**
 * Update achievement tracker (called each game turn).
 * Checks conditions and unlocks achievements.
 */
void achievement_tracker_update(void);

/**
 * Track level completion.
 */
void achievement_tracker_level_complete(void);

/**
 * Track creature spawn/usage.
 * @param creature_model Creature model ID.
 */
void achievement_tracker_creature_spawned(int creature_model);

/**
 * Track creature death.
 * @param creature_model Creature model ID.
 * @param is_friendly True if friendly creature.
 */
void achievement_tracker_creature_died(int creature_model, TbBool is_friendly);

/**
 * Track creature kill.
 * @param killer_model Killer creature model.
 * @param victim_model Victim creature model.
 */
void achievement_tracker_creature_killed(int killer_model, int victim_model);

/**
 * Track slap usage.
 * @param thing The thing being slapped (can be NULL for global tracking).
 */
void achievement_tracker_slap_used(struct Thing *thing);

/**
 * Track battle result.
 * @param won True if player won the battle.
 */
void achievement_tracker_battle(TbBool won);

/**
 * Track heart destruction.
 */
void achievement_tracker_heart_destroyed(void);

/**
 * Track gold expenditure.
 * @param amount Amount of gold spent.
 */
void achievement_tracker_gold_spent(long amount);

/**
 * Track room construction.
 * @param room_kind Room type.
 */
void achievement_tracker_room_built(int room_kind);

/**
 * Track spell usage.
 * @param spell_kind Spell type.
 */
void achievement_tracker_spell_used(int spell_kind);

/**
 * Track trap usage.
 * @param trap_kind Trap type.
 */
void achievement_tracker_trap_used(int trap_kind);

/**
 * Unlock achievement by script flag.
 * @param flag_id Script flag ID.
 */
void achievement_tracker_script_flag(int flag_id);

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // ACHIEVEMENT_TRACKER_H