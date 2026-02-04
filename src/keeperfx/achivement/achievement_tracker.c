/******************************************************************************/
/** @file achievement_tracker.c
 *     Achievement tracking and condition checking.
 * @par Purpose:
 *     Monitors game events and unlocks achievements when conditions are met.
 * @par Comment:
 *     Core achievement tracking logic.
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
#include "achievement_tracker.h"

#include <string.h>
#include "achievement_api.h"
#include "achievement_definitions.h"
#include "../../game_legacy.h"
#include "../../keeperfx.hpp"
#include "../../post_inc.h"

/******************************************************************************/
struct AchievementTracker achievement_tracker;

/******************************************************************************/
/**
 * Initialize achievement tracker for new level.
 * @param level_num Level number.
 * @todo Load active achievements for this level.
 * @note This would scan all registered achievements and find ones that apply to this level.
 */
void achievement_tracker_init(LevelNumber level_num)
{
    memset(&achievement_tracker, 0, sizeof(achievement_tracker));
    achievement_tracker.current_level = level_num;
    achievement_tracker.level_start_time = game.play_gameturn;
    
    SYNCLOG("Achievement tracker initialized for level %d", level_num);
}

/**
 * Reset tracker state (called on level start).
 */
void achievement_tracker_reset(void)
{
    achievement_tracker.creature_deaths = 0;
    achievement_tracker.slaps_used = 0;
    achievement_tracker.enemy_kills = 0;
    achievement_tracker.battles_won = 0;
    achievement_tracker.hearts_destroyed = 0;
    achievement_tracker.gold_spent = 0;
    achievement_tracker.level_completed = false;
    achievement_tracker.creature_types_used = 0;
    achievement_tracker.rooms_built = 0;
    achievement_tracker.spells_used = 0;
    achievement_tracker.traps_used = 0;
    memset(achievement_tracker.max_creatures, 0, sizeof(achievement_tracker.max_creatures));
}

/**
 * Update achievement tracker (called each game turn).
 * Checks conditions and unlocks achievements.
 * @note This is where we'd evaluate achievement conditions.
 * @todo Iterate through active achievements and check conditions.
 * @todo Unlock achievements when all conditions are met.
 */
void achievement_tracker_update(void)
{
}

/**
 * Track level completion.
 * @todo Check all achievements that require level completion.
 * @note This is where most achievements would be evaluated.
 * @note Example logic: for each active achievement, if all conditions met, achievement_unlock(achievement_id).
 */
void achievement_tracker_level_complete(void)
{
    achievement_tracker.level_completed = true;
    
    SYNCLOG("Level completed, checking achievements");
}

/**
 * Track creature spawn/usage.
 * @param creature_model Creature model ID.
 * @todo Get current creature count from game state.
 */
void achievement_tracker_creature_spawned(int creature_model)
{
    if (creature_model < 0 || creature_model >= 32)
        return;
    
    /// Mark creature type as used
    achievement_tracker.creature_types_used |= (1 << creature_model);
}

/**
 * Track creature death.
 * @param creature_model Creature model ID.
 * @param is_friendly True if friendly creature.
 */
void achievement_tracker_creature_died(int creature_model, TbBool is_friendly)
{
    if (is_friendly)
    {
        achievement_tracker.creature_deaths++;
        SYNCLOG("Friendly creature died, total deaths: %ld", achievement_tracker.creature_deaths);
    }
}

/**
 * Track creature kill.
 * @param killer_model Killer creature model.
 * @param victim_model Victim creature model.
 * @todo Track specific creature kills for achievements like "kill an Avatar with a converted Avatar".
 */
void achievement_tracker_creature_killed(int killer_model, int victim_model)
{
    achievement_tracker.enemy_kills++;
}

/**
 * Track slap usage.
 */
void achievement_tracker_slap_used(void)
{
    achievement_tracker.slaps_used++;
    SYNCLOG("Slap used, total slaps: %ld", achievement_tracker.slaps_used);
}

/**
 * Track battle result.
 * @param won True if player won the battle.
 */
void achievement_tracker_battle(TbBool won)
{
    if (won)
    {
        achievement_tracker.battles_won++;
    }
}

/**
 * Track heart destruction.
 */
void achievement_tracker_heart_destroyed(void)
{
    achievement_tracker.hearts_destroyed++;
    SYNCLOG("Heart destroyed, total: %ld", achievement_tracker.hearts_destroyed);
}

/**
 * Track gold expenditure.
 * @param amount Amount of gold spent.
 */
void achievement_tracker_gold_spent(long amount)
{
    achievement_tracker.gold_spent += amount;
}

/**
 * Track room construction.
 * @param room_kind Room type.
 */
void achievement_tracker_room_built(int room_kind)
{
    if (room_kind < 0 || room_kind >= 32)
        return;
    
    achievement_tracker.rooms_built |= (1 << room_kind);
}

/**
 * Track spell usage.
 * @param spell_kind Spell type.
 */
void achievement_tracker_spell_used(int spell_kind)
{
    if (spell_kind < 0 || spell_kind >= 32)
        return;
    
    achievement_tracker.spells_used |= (1 << spell_kind);
}

/**
 * Track trap usage.
 * @param trap_kind Trap type.
 */
void achievement_tracker_trap_used(int trap_kind)
{
    if (trap_kind < 0 || trap_kind >= 32)
        return;
    
    achievement_tracker.traps_used |= (1 << trap_kind);
}

/**
 * Unlock achievement by script flag.
 * @param flag_id Script flag ID.
 * @todo Find and unlock achievements that match this script flag.
 * @note This allows level scripts to trigger achievements directly.
 * @note Example: for each registered achievement, if achievement has SCRIPT_FLAG condition with flag_id, achievement_unlock(achievement_id).
 */
void achievement_tracker_script_flag(int flag_id)
{
    SYNCLOG("Achievement script flag triggered: %d", flag_id);
}

/******************************************************************************/