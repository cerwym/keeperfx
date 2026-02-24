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
#include "achievement_save.h"

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

static struct AchievementBackend* registered_backend = NULL;
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
    
    // Clear achievement array
    memset(achievements, 0, sizeof(achievements));
    achievements_count = 0;
    
    // Detect platform
    current_platform = achievements_detect_platform();
    
    // Initialize platform backend if available
    if (registered_backend != NULL && registered_backend->init != NULL)
    {
        if (!registered_backend->init())
        {
            WARNLOG("Failed to initialize %s achievement backend", 
                    registered_backend->name);
            // Continue with local storage as fallback
            current_platform = AchPlat_Local;
        }
        else
        {
            SYNCLOG("Achievement backend initialized: %s", registered_backend->name);
        }
    }
    
    achievements_initialized = true;
    return true;
}

void achievements_shutdown(void)
{
    if (!achievements_initialized)
        return;
    
    SYNCLOG("Shutting down achievement system");
    
    // Shutdown platform backend
    if (registered_backend != NULL && registered_backend->shutdown != NULL)
    {
        registered_backend->shutdown();
    }
    
    achievements_initialized = false;
    registered_backend = NULL;
}

void achievements_update(void)
{
    if (!achievements_initialized)
        return;
    
    // Sync with platform backend if available
    if (registered_backend != NULL && registered_backend->sync != NULL)
    {
        registered_backend->sync();
    }
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
    
    // Notify platform backend
    if (registered_backend != NULL && registered_backend->unlock != NULL)
    {
        if (!registered_backend->unlock(achievement_id))
        {
            WARNLOG("Platform backend failed to unlock achievement: %s", achievement_id);
        }
    }
    
    // Create in-game event notification for achievement unlock
    extern struct Event *event_create_event(MapCoord map_x, MapCoord map_y, EventKind evkind, unsigned char dngn_id, int32_t target);
    
    // Find achievement index to pass as target
    int ach_idx = ach - achievements;
    event_create_event(0, 0, EvKind_AchievementUnlocked, 0, ach_idx);
    
    // Persist unlock state immediately
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
    
    // Notify platform backend
    if (registered_backend != NULL && registered_backend->set_progress != NULL)
    {
        registered_backend->set_progress(achievement_id, progress);
    }
    
    // Persist partial progress
    save_achievement_state();
}

void achievements_clear(void)
{
    SYNCLOG("Clearing achievements");
    memset(achievements, 0, sizeof(achievements));
    achievements_count = 0;
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
    
    registered_backend = backend;
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