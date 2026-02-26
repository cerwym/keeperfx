/******************************************************************************/
/** @file achievement_api.h
 *     Header file for achievement_api.c.
 * @par Purpose:
 *     Cross-platform achievement system API.
 * @par Comment:
 *     Provides a platform-agnostic interface for tracking and unlocking
 *     achievements across Steam, GOG, Xbox, PlayStation, and other platforms.
 * @author   Peter Lockett & KeeperFX Team
 * @date     02 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef ACHIEVEMENT_API_H
#define ACHIEVEMENT_API_H

#include "../../bflib_basics.h"
#include "../../globals.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
#define ACHIEVEMENT_ID_LEN 64
#define ACHIEVEMENT_NAME_LEN 256
#define ACHIEVEMENT_DESC_LEN 512
#define ACHIEVEMENT_ICON_PATH_LEN 128
#define MAX_ACHIEVEMENTS_PER_CAMPAIGN 100
#define MAX_PLATFORM_IDS 8
#define PLATFORM_KEY_LEN 16
#define PLATFORM_ID_LEN 64
#define ACHIEVEMENT_SCOPE_GLOBAL "global"

/** Maps an achievement to its platform-specific API key */
struct PlatformIdMapping {
    char platform[PLATFORM_KEY_LEN];  ///< Platform key: "gog", "steam", "xbox", "psn", "keeperfx"
    char api_key[PLATFORM_ID_LEN];    ///< Platform-specific achievement API key
};

/******************************************************************************/
/** Enumeration of supported achievement platforms */
enum AchievementPlatform {
    AchPlat_None = 0,      ///< No platform / local only
    AchPlat_Steam,         ///< Steam / Steamworks
    AchPlat_GOG,           ///< GOG Galaxy
    AchPlat_Xbox,          ///< Xbox (PC and Console)
    AchPlat_PlayStation,   ///< PlayStation 4/5/Vita
    AchPlat_Epic,          ///< Epic Games Store
    AchPlat_Local,         ///< Local file storage (fallback)
};

/** Structure representing a single achievement */
struct Achievement {
    char id[ACHIEVEMENT_ID_LEN];              ///< Unique identifier (campaign.id)
    char name[ACHIEVEMENT_NAME_LEN];          ///< Display name (fallback if text ID fails)
    char description[ACHIEVEMENT_DESC_LEN];   ///< Description (fallback if text ID fails)
    char icon_path[ACHIEVEMENT_ICON_PATH_LEN];///< Path to icon image
    int points;                               ///< Point/Gamerscore value
    TbBool hidden;                            ///< Hidden until unlocked
    TbBool unlocked;                          ///< Current unlock state
    time_t unlock_time;                       ///< Unix timestamp of unlock (0 if locked)
    float progress;                           ///< Progress towards unlock (0.0-1.0)
    int name_text_id;                         ///< String ID for localized name (0 = use name field)
    int desc_text_id;                         ///< String ID for localized description (0 = use description field)
    struct PlatformIdMapping platform_ids[MAX_PLATFORM_IDS]; ///< Platform-specific API keys
    int platform_id_count;                    ///< Number of platform ID mappings
};

/** Platform-specific achievement backend interface */
struct AchievementBackend {
    const char* name;                         ///< Backend name
    enum AchievementPlatform platform_type;   ///< Platform type
    
    /** @name Lifecycle */
    /**@{*/
    TbBool (*init)(void);                     ///< Initialize backend
    void (*shutdown)(void);                   ///< Shutdown backend
    /**@}*/
    
    /** @name Achievement operations */
    /**@{*/
    TbBool (*unlock)(const char* achievement_id);              ///< Unlock achievement
    TbBool (*is_unlocked)(const char* achievement_id);         ///< Check if unlocked
    TbBool (*set_progress)(const char* achievement_id, float progress); ///< Set progress
    float (*get_progress)(const char* achievement_id);         ///< Get progress
    TbBool (*clear)(const char* achievement_id);               ///< Clear/re-lock achievement
    /**@}*/
    
    /** @name Optional batch operations */
    /**@{*/
    void (*sync)(void);                       ///< Sync with platform backend
    /**@}*/

    TbBool needs_worker_thread;               ///< If true, IntegrationManager spawns a dedicated thread
};

/******************************************************************************/
// Global achievement system state
extern struct Achievement achievements[MAX_ACHIEVEMENTS_PER_CAMPAIGN];
extern int achievements_count;
extern enum AchievementPlatform current_platform;

/******************************************************************************/
// Core API functions

/**
 * Initialize the achievement system.
 * Detects available platforms and loads achievement data.
 * @return True if initialization successful.
 */
TbBool achievements_init(void);

/**
 * Shutdown the achievement system.
 * Saves progress and releases platform resources.
 */
void achievements_shutdown(void);

/**
 * Update achievement system (call each game turn).
 * Processes pending achievement checks and platform callbacks.
 */
void achievements_update(void);

/**
 * Unlock an achievement by ID.
 * @param achievement_id The unique achievement identifier.
 * @return True if successfully unlocked (or already unlocked).
 */
TbBool achievement_unlock(const char* achievement_id);

/**
 * Check if an achievement is unlocked.
 * @param achievement_id The unique achievement identifier.
 * @return True if the achievement is unlocked.
 */
TbBool achievement_is_unlocked(const char* achievement_id);

/**
 * Get achievement progress (0.0 to 1.0).
 * @param achievement_id The unique achievement identifier.
 * @return Progress value, or -1.0 if achievement not found.
 */
float achievement_get_progress(const char* achievement_id);

/**
 * Set achievement progress (for incremental achievements).
 * @param achievement_id The unique achievement identifier.
 * @param progress Progress value (0.0 to 1.0).
 */
void achievement_set_progress(const char* achievement_id, float progress);

/**
 * Find achievement by ID.
 * @param achievement_id The unique achievement identifier.
 * @return Pointer to achievement structure, or NULL if not found.
 */
struct Achievement* achievement_find(const char* achievement_id);

/**
 * Register an achievement.
 * @param achievement Achievement data to register.
 * @return True if successfully registered.
 */
TbBool achievement_register(struct Achievement* achievement);

/**
 * Clear all achievements (for new campaign).
 */
void achievements_clear(void);

/**
 * Reset all achievement unlock state (local + platform backend).
 * Clears unlocked flag, progress, and deletes the save file.
 * @return True if reset successful.
 */
TbBool achievements_reset_all(void);

/**
 * Reset a single achievement's unlock state (local + platform backend).
 * @param achievement_id The unique achievement identifier.
 * @return True if reset successful.
 */
TbBool achievement_reset(const char* achievement_id);

/**
 * Reset only campaign-scoped achievement unlock state.
 * Resets achievements whose ID starts with the given campaign prefix.
 * @param campaign_name Campaign scope to reset (e.g. "keeporig"). NULL = current campaign.
 * @return True if reset successful.
 */
TbBool achievements_reset_campaign(const char* campaign_name);

/**
 * Reset only global-scoped achievement unlock state.
 * Resets achievements whose ID starts with "global.".
 * @return True if reset successful.
 */
TbBool achievements_reset_global(void);

/**
 * Get total achievement count.
 * @return Number of registered achievements.
 */
int achievements_get_count(void);

/**
 * Get number of unlocked achievements.
 * @return Number of unlocked achievements.
 */
int achievements_get_unlocked_count(void);

/**
 * Get achievement completion percentage.
 * @return Completion percentage (0.0 to 100.0).
 */
float achievements_get_completion_percentage(void);

/**
 * Get the platform-specific API key for an achievement.
 * Looks up the platform_ids mapping for the given platform key.
 * Falls back to the bare definition ID (part after last dot) if no mapping exists.
 * @param achievement_id Full namespaced achievement ID (e.g. "keeporig.imp_slapped").
 * @param platform Platform key (e.g. "gog", "steam", "xbox", "psn", "keeperfx").
 * @return Platform-specific API key string, or bare def ID as fallback. Never NULL.
 */
const char* achievement_get_platform_id(const char* achievement_id, const char* platform);

/******************************************************************************/
// Platform detection and selection

/**
 * Detect available achievement platforms.
 * @return Detected platform type.
 */
enum AchievementPlatform achievements_detect_platform(void);

/**
 * Get the name of a platform.
 * @param platform Platform type.
 * @return Platform name string.
 */
const char* achievements_get_platform_name(enum AchievementPlatform platform);

/**
 * Register a platform backend.
 * @param backend Backend implementation.
 * @return True if successfully registered.
 */
TbBool achievements_register_backend(struct AchievementBackend* backend);

/**
 * Get achievement name by index.
 * @param ach_idx Achievement index.
 * @return Achievement name or NULL if invalid.
 */
const char* achievement_get_name_by_index(int ach_idx);

/**
 * Get achievement description by index.
 * @param ach_idx Achievement index.
 * @return Achievement description or NULL if invalid.
 */
const char* achievement_get_description_by_index(int ach_idx);

/**
 * Get total number of registered achievements.
 * @return Total achievement count.
 */
int achievement_count(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif

#endif // ACHIEVEMENT_API_H