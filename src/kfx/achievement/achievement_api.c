/******************************************************************************/
/** @file achievement_api.c
 *     Cross-platform achievement system implementation.
 * @par Purpose:
 *     Provides platform-agnostic achievement tracking and unlocking.
 * @par Comment:
 *     Core achievement system with support for multiple platforms.
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
#include "achievement_api.h"
#include "achievement_definitions.h"
#include "achievement_save.h"
#include "achievement_tracker.h"
#include "../integration/integration_bridge.h"

#include <string.h>
#include <stdio.h>
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../keeperfx.hpp"
#include "../../post_inc.h"

/******************************************************************************/
// Global state
struct Achievement achievements[MAX_ACHIEVEMENTS_PER_CAMPAIGN];
int achievements_count = 0;
enum AchievementPlatform current_platform = AchPlat_None;

static TbBool achievements_initialized = false;

/******************************************************************************/
// Platform name mapping
const char* achievements_get_platform_name(enum AchievementPlatform platform)
{
    switch(platform)
    {
        case AchPlat_Steam: return "Steam";
        case AchPlat_GOG: return "GOG Galaxy";
        case AchPlat_Xbox: return "Xbox";
        case AchPlat_PlayStation: return "PlayStation";
        case AchPlat_Epic: return "Epic Games Store";
        case AchPlat_Local: return "Local Storage";
        default: return "None";
    }
}

/******************************************************************************/
// Platform detection
enum AchievementPlatform achievements_detect_platform(void)
{
    // Check for Steam
#ifdef _WIN32
    if (LbFileExists("steam_api.dll") && LbFileExists("steam_appid.txt"))
    {
        SYNCDBG(8, "Achievement platform detected: Steam");
        return AchPlat_Steam;
    }
    
    // Check for GOG Galaxy
    if (LbFileExists("Galaxy.dll"))
    {
        SYNCDBG(8, "Achievement platform detected: GOG Galaxy");
        return AchPlat_GOG;
    }
#endif
    
    // TODO: Add Xbox, PlayStation, Epic detection
    
    // Fallback to local storage
    SYNCDBG(8, "Achievement platform: Local storage (no platform SDK detected)");
    return AchPlat_Local;
}

/******************************************************************************/
// Core API implementation

TbBool achievements_init(void)
{
    if (achievements_initialized)
    {
        WARNLOG("Achievement system already initialized");
        return true;
    }
    
    SYNCLOG("Initializing achievement system");
    
    // Don't clear the runtime arrays here — change_campaign() may have
    // already loaded campaign achievements before this runs. The arrays
    // are zero-initialized as globals.
    
    // Detect platform
    current_platform = achievements_detect_platform();
    
    // Backend initialization is now handled by IntegrationManager
    // which was started before this call
    
    // Load global achievements (persist across campaign switches)
    load_global_achievements();
    load_achievement_state_global();
    
    achievements_initialized = true;
    return true;
}

void achievements_shutdown(void)
{
    if (!achievements_initialized)
        return;
    
    SYNCLOG("Shutting down achievement system");
    
    // Backend shutdown is handled by IntegrationManager
    
    achievements_initialized = false;
}

void achievements_update(void)
{
    if (!achievements_initialized)
        return;
    
    // Pump non-threaded backends via IntegrationManager
    integration_manager_sync();
}

/******************************************************************************/
// Achievement operations

struct Achievement* achievement_find(const char* achievement_id)
{
    if (achievement_id == NULL)
        return NULL;
    
    for (int i = 0; i < achievements_count; i++)
    {
        if (strcmp(achievements[i].id, achievement_id) == 0)
        {
            return &achievements[i];
        }
    }
    
    return NULL;
}

const char* achievement_get_platform_id(const char* achievement_id, const char* platform)
{
    static char fallback_buf[PLATFORM_ID_LEN];
    
    struct Achievement* ach = achievement_find(achievement_id);
    if (ach != NULL)
    {
        for (int i = 0; i < ach->platform_id_count; i++)
        {
            if (strcmp(ach->platform_ids[i].platform, platform) == 0)
                return ach->platform_ids[i].api_key;
        }
    }
    
    // Fallback: strip campaign prefix (everything after last dot)
    const char* dot = strrchr(achievement_id, '.');
    if (dot != NULL)
    {
        strncpy(fallback_buf, dot + 1, PLATFORM_ID_LEN - 1);
        fallback_buf[PLATFORM_ID_LEN - 1] = '\0';
        return fallback_buf;
    }
    
    return achievement_id;
}

TbBool achievement_register(struct Achievement* achievement)
{
    if (achievement == NULL)
    {
        ERRORLOG("Cannot register NULL achievement");
        return false;
    }
    
    if (achievements_count >= MAX_ACHIEVEMENTS_PER_CAMPAIGN)
    {
        ERRORLOG("Achievement limit reached (%d)", MAX_ACHIEVEMENTS_PER_CAMPAIGN);
        return false;
    }
    
    // Check for duplicate
    if (achievement_find(achievement->id) != NULL)
    {
        WARNLOG("Achievement '%s' already registered", achievement->id);
        return false;
    }
    
    // Copy achievement data
    memcpy(&achievements[achievements_count], achievement, sizeof(struct Achievement));
    achievements_count++;
    
    SYNCDBG(8, "Registered achievement: %s (%s)", achievement->id, achievement->name);
    return true;
}

TbBool achievement_unlock(const char* achievement_id)
{
    if (!achievements_initialized)
    {
        WARNLOG("Achievement system not initialized");
        return false;
    }
    
    struct Achievement* ach = achievement_find(achievement_id);
    if (ach == NULL)
    {
        WARNLOG("Achievement not found: %s", achievement_id);
        return false;
    }
    
    // Already unlocked
    if (ach->unlocked)
    {
        return true;
    }
    
    // Mark as unlocked
    ach->unlocked = true;
    ach->unlock_time = time(NULL);
    ach->progress = 1.0f;
    
    SYNCLOG("Achievement unlocked: %s (%s)", ach->id, ach->name);
    
    // Notify all platform backends via IntegrationManager
    integration_manager_unlock(achievement_id);
    
    // Create in-game event notification for achievement unlock
    extern struct Event *event_create_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int32_t target);
    
    // Find achievement index to pass as target
    int ach_idx = ach - achievements;
    event_create_event(0, 0, EvKind_AchievementUnlocked, 0, ach_idx);
    
    // Persist unlock state immediately to the correct file
    if (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0)
        save_achievement_state_global();
    else
        save_achievement_state();
    
    return true;
}

TbBool achievement_is_unlocked(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    if (ach == NULL)
        return false;
    
    return ach->unlocked;
}

float achievement_get_progress(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    if (ach == NULL)
        return -1.0f;
    
    return ach->progress;
}

void achievement_set_progress(const char* achievement_id, float progress)
{
    struct Achievement* ach = achievement_find(achievement_id);
    if (ach == NULL)
    {
        WARNLOG("Cannot set progress for unknown achievement: %s", achievement_id);
        return;
    }
    
    // Clamp progress to valid range
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    
    // Progress is monotonic — can only increase
    if (progress <= ach->progress)
        return;
    
    ach->progress = progress;
    
    // Auto-unlock if progress reaches 100%
    if (progress >= 1.0f && !ach->unlocked)
    {
        achievement_unlock(achievement_id);
        return; // unlock already saves state
    }
    
    // Notify all platform backends via IntegrationManager
    integration_manager_set_progress(achievement_id, progress);
    
    // Persist partial progress to the correct file
    if (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0)
        save_achievement_state_global();
    else
        save_achievement_state();
}

void achievements_clear(void)
{
    SYNCLOG("Clearing campaign achievements (preserving globals)");
    
    // Compact: keep global achievements, remove campaign-scoped ones
    int write_idx = 0;
    for (int i = 0; i < achievements_count; i++)
    {
        if (strncmp(achievements[i].id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                    sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0)
        {
            if (write_idx != i)
                memcpy(&achievements[write_idx], &achievements[i], sizeof(struct Achievement));
            write_idx++;
        }
    }
    
    // Zero out the freed slots
    if (write_idx < achievements_count)
        memset(&achievements[write_idx], 0,
               (achievements_count - write_idx) * sizeof(struct Achievement));
    
    achievements_count = write_idx;
    SYNCDBG(8, "Cleared campaign achievements, %d globals retained", write_idx);
    
    // Also clear campaign-scoped definitions (keep global ones)
    int def_write = 0;
    for (int i = 0; i < achievement_definitions_count; i++)
    {
        if (strcmp(achievement_definitions[i].campaign_name, ACHIEVEMENT_SCOPE_GLOBAL) == 0)
        {
            if (def_write != i)
                memcpy(&achievement_definitions[def_write], &achievement_definitions[i],
                       sizeof(struct AchievementDefinition));
            def_write++;
        }
    }
    if (def_write < achievement_definitions_count)
        memset(&achievement_definitions[def_write], 0,
               (achievement_definitions_count - def_write) * sizeof(struct AchievementDefinition));
    achievement_definitions_count = def_write;
}

TbBool achievements_reset_all(void)
{
    SYNCLOG("Resetting all achievement unlock state");

    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        if (!ach->unlocked && ach->progress <= 0.0f)
            continue;

        // Clear on all platform backends
        integration_manager_clear(ach->id);

        ach->unlocked = false;
        ach->unlock_time = 0;
        ach->progress = 0.0f;
    }

    // Persist the cleared state (writes clean/empty JSON)
    save_achievement_state();
    save_achievement_state_global();

    // Reset tracker counters so conditions aren't immediately re-met
    achievement_tracker_reset();

    SYNCLOG("All achievements reset (%d cleared)", achievements_count);
    return true;
}

TbBool achievement_reset(const char* achievement_id)
{
    struct Achievement *ach = achievement_find(achievement_id);
    if (ach == NULL)
    {
        WARNLOG("Cannot reset unknown achievement: %s", achievement_id);
        return false;
    }

    integration_manager_clear(ach->id);

    ach->unlocked = false;
    ach->unlock_time = 0;
    ach->progress = 0.0f;

    // Persist to the correct file based on scope
    if (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0)
        save_achievement_state_global();
    else
        save_achievement_state();

    // Reset tracker counters so conditions aren't immediately re-met
    achievement_tracker_reset();

    SYNCLOG("Achievement reset: %s", ach->id);
    return true;
}

TbBool achievements_reset_campaign(const char* campaign_name)
{
    SYNCLOG("Resetting campaign achievements for: %s",
            campaign_name ? campaign_name : "(current)");
    int count = 0;

    // Build prefix to match (e.g. "keeporig.")
    char prefix[80];
    if (campaign_name != NULL)
        snprintf(prefix, sizeof(prefix), "%s.", campaign_name);
    else
        prefix[0] = '\0'; // match all non-global

    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        // Skip globals
        if (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                    sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) == 0)
            continue;
        // If a specific campaign was given, filter by prefix
        if (prefix[0] != '\0' && strncmp(ach->id, prefix, strlen(prefix)) != 0)
            continue;

        if (!ach->unlocked && ach->progress <= 0.0f)
            continue;

        integration_manager_clear(ach->id);
        ach->unlocked = false;
        ach->unlock_time = 0;
        ach->progress = 0.0f;
        count++;
    }

    save_achievement_state();
    achievement_tracker_reset();
    SYNCLOG("Campaign achievements reset (%d cleared)", count);
    return true;
}

TbBool achievements_reset_global(void)
{
    SYNCLOG("Resetting global achievements");
    int count = 0;

    for (int i = 0; i < achievements_count; i++)
    {
        struct Achievement *ach = &achievements[i];
        if (strncmp(ach->id, ACHIEVEMENT_SCOPE_GLOBAL ".",
                    sizeof(ACHIEVEMENT_SCOPE_GLOBAL)) != 0)
            continue;

        if (!ach->unlocked && ach->progress <= 0.0f)
            continue;

        integration_manager_clear(ach->id);
        ach->unlocked = false;
        ach->unlock_time = 0;
        ach->progress = 0.0f;
        count++;
    }

    save_achievement_state_global();
    achievement_tracker_reset();
    SYNCLOG("Global achievements reset (%d cleared)", count);
    return true;
}

int achievements_get_count(void)
{
    return achievements_count;
}

int achievements_get_unlocked_count(void)
{
    int count = 0;
    for (int i = 0; i < achievements_count; i++)
    {
        if (achievements[i].unlocked)
            count++;
    }
    return count;
}

float achievements_get_completion_percentage(void)
{
    if (achievements_count == 0)
        return 0.0f;
    
    int unlocked = achievements_get_unlocked_count();
    return (float)unlocked / (float)achievements_count * 100.0f;
}

TbBool achievements_register_backend(struct AchievementBackend* backend)
{
    if (backend == NULL)
    {
        ERRORLOG("Cannot register NULL achievement backend");
        return false;
    }
    
    // Route through IntegrationManager for multi-backend support
    integration_manager_register_backend(backend);
    SYNCLOG("Registered achievement backend: %s", backend->name);
    return true;
}

const char* achievement_get_name_by_index(int ach_idx)
{
    if (ach_idx < 0 || ach_idx >= achievements_count)
        return NULL;
    
    return achievements[ach_idx].name;
}

const char* achievement_get_description_by_index(int ach_idx)
{
    if (ach_idx < 0 || ach_idx >= achievements_count)
        return NULL;
    
    return achievements[ach_idx].description;
}

int achievement_count(void)
{
    return achievements_count;
}

/******************************************************************************/