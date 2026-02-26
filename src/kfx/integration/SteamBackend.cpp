/******************************************************************************/
/** @file SteamBackend.cpp
 *     Steam API integration for KeeperFX.
 * @par Purpose:
 *     Loads steam_api.dll at runtime and initializes the Steam API.
 *     Implements AchievementBackend with stubbed achievement operations
 *     (Steam achievement support to be added later via ISteamUserStats).
 * @par Comment:
 *     Originally extracted from steam_api.cpp.
 *     The following files need to be present in the KeeperFX directory:
 *     - steam_api.dll (from Steamworks SDK)
 *     - steam_appid.txt (containing Steam App ID 1996630)
 *     Documentation: https://partner.steamgames.com/doc/sdk/api
 *
 * @author   KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "SteamBackend.h"
#include "IntegrationManager.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "../../keeperfx.hpp"
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../post_inc.h"

/******************************************************************************/
#ifdef _WIN32

/// Steam API types
enum ESteamAPIInitResult
{
    k_ESteamAPIInitResult_OK = 0,
    k_ESteamAPIInitResult_FailedGeneric = 1,
    k_ESteamAPIInitResult_NoSteamClient = 2,
    k_ESteamAPIInitResult_VersionMismatch = 3,
};

typedef char SteamErrMsg[1024];
typedef ESteamAPIInitResult (__cdecl *SteamApiInitFunc)(SteamErrMsg *err);
typedef void (__cdecl *SteamApiShutdownFunc)();

union SteamApiInitUnion { FARPROC farProc; SteamApiInitFunc steamApiInitFunc; };

/// Module state
static HMODULE steam_lib = NULL;
static SteamApiInitFunc SteamAPI_Init = NULL;
static SteamApiShutdownFunc SteamAPI_Shutdown = NULL;

/******************************************************************************/
/// AchievementBackend implementation

static TbBool steam_backend_init(void)
{
    if (steam_lib != NULL || SteamAPI_Init != NULL)
    {
        WARNLOG("Steam API already initialized");
        return true;
    }

    if (!LbFileExists("steam_api.dll") || !LbFileExists("steam_appid.txt"))
    {
        if (LbFileExists("steam_api.dll") != LbFileExists("steam_appid.txt"))
            ERRORLOG("The Steam API requires both 'steam_api.dll' and 'steam_appid.txt'");
        return false;
    }

    JUSTLOG("'steam_api.dll' and 'steam_appid.txt' found");

    if (is_running_under_wine)
    {
        WARNLOG("Using the Steam API under Wine is not supported");
        return false;
    }

    steam_lib = LoadLibraryA("steam_api.dll");
    if (!steam_lib)
    {
        ERRORLOG("Unable to load 'steam_api.dll' library");
        return false;
    }

    JUSTLOG("'steam_api.dll' library loaded");

    SteamApiInitUnion u_init;
    u_init.farProc = GetProcAddress(steam_lib, "SteamAPI_InitFlat");
    if (!u_init.farProc)
    {
        ERRORLOG("Failed to get proc address for 'SteamAPI_InitFlat'");
        FreeLibrary(steam_lib);
        steam_lib = NULL;
        return false;
    }
    SteamAPI_Init = u_init.steamApiInitFunc;

    SteamAPI_Shutdown = reinterpret_cast<SteamApiShutdownFunc>(
        GetProcAddress(steam_lib, "SteamAPI_Shutdown"));
    if (!SteamAPI_Shutdown)
    {
        ERRORLOG("Failed to get proc address for 'SteamAPI_Shutdown'");
        FreeLibrary(steam_lib);
        steam_lib = NULL;
        SteamAPI_Init = NULL;
        return false;
    }

    SteamErrMsg error;
    ESteamAPIInitResult result = SteamAPI_Init(&error);
    if (result != k_ESteamAPIInitResult_OK)
    {
        JUSTLOG("Steam API Failure: %s", error);
        FreeLibrary(steam_lib);
        steam_lib = NULL;
        SteamAPI_Init = NULL;
        SteamAPI_Shutdown = NULL;
        return false;
    }

    JUSTLOG("Steam API initialized");
    return true;
}

static void steam_backend_shutdown(void)
{
    if (SteamAPI_Shutdown)
        SteamAPI_Shutdown();

    SteamAPI_Shutdown = NULL;
    SteamAPI_Init = NULL;

    if (steam_lib)
    {
        FreeLibrary(steam_lib);
        steam_lib = NULL;
    }
}

// Achievement operations — stubbed until ISteamUserStats integration
static TbBool steam_backend_unlock(const char* achievement_id)
{
    (void)achievement_id;
    // TODO: ISteamUserStats::SetAchievement + StoreStats
    return true;
}

static TbBool steam_backend_is_unlocked(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    return (ach != NULL && ach->unlocked) ? true : false;
}

static TbBool steam_backend_set_progress(const char* achievement_id, float progress)
{
    (void)achievement_id;
    (void)progress;
    return true;
}

static float steam_backend_get_progress(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    return (ach != NULL) ? ach->progress : 0.0f;
}

static TbBool steam_backend_clear(const char* achievement_id)
{
    (void)achievement_id;
    // TODO: ISteamUserStats::ClearAchievement + StoreStats
    return true;
}

static void steam_backend_sync(void)
{
    // TODO: SteamAPI_RunCallbacks if needed
}

/******************************************************************************/
static struct AchievementBackend steam_backend = {
    "Steam",                    // name
    AchPlat_Steam,              // platform_type
    steam_backend_init,         // init
    steam_backend_shutdown,     // shutdown
    steam_backend_unlock,       // unlock
    steam_backend_is_unlocked,  // is_unlocked
    steam_backend_set_progress, // set_progress
    steam_backend_get_progress, // get_progress
    steam_backend_clear,        // clear
    steam_backend_sync,         // sync
    false,                      // needs_worker_thread
};

#endif // _WIN32

/******************************************************************************/
int steam_backend_register(void)
{
#ifndef _WIN32
    return -1;
#else
    if (!LbFileExists("steam_api.dll") || !LbFileExists("steam_appid.txt"))
        return 1;

    if (is_running_under_wine)
        return 1;

    integration_manager_register_backend(&steam_backend);
    JUSTLOG("Steam: Backend registered with IntegrationManager");
    return 0;
#endif
}
