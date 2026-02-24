/******************************************************************************/
/** @file gog_galaxy_api.cpp
 *     GOG Galaxy SDK integration for KeeperFX achievements.
 * @par Purpose:
 *     Dynamically loads Galaxy.dll at runtime and provides achievement
 *     operations via the AchievementBackend interface.
 * @par Comment:
 *     Uses MSVC-mangled export names with GetProcAddress since Galaxy.dll
 *     is compiled with MSVC. The C++ vtable layout is binary-compatible
 *     between MSVC and MinGW on Win32 for single-inheritance classes.
 * @author   Peter Lockett & KeeperFX Team
 * @date     24 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "gog_galaxy_api.h"
#include "achievement_api.h"
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../post_inc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../keeperfx.hpp"
#endif

/******************************************************************************/
// GOG Galaxy Client Credentials (public — embedded in all GOG game binaries)
#define GOG_CLIENT_ID     "58850072615442688"
#define GOG_CLIENT_SECRET "972283970fbaad55d7de7b8a34212598913d741d1314952eeedc054cd1df4434"

/******************************************************************************/
#ifdef _WIN32

/// Galaxy SDK uses MSVC C++ name mangling. These are the exact export names
/// from Galaxy.dll (32-bit MSVC19 build, SDK v1.152.10).
static const char* const GALAXY_EXPORT_INIT        = "?Init@api@galaxy@@YAXABUInitOptions@12@@Z";
static const char* const GALAXY_EXPORT_SHUTDOWN     = "?Shutdown@api@galaxy@@YAXXZ";
static const char* const GALAXY_EXPORT_PROCESS_DATA = "?ProcessData@api@galaxy@@YAXXZ";
static const char* const GALAXY_EXPORT_STATS        = "?Stats@api@galaxy@@YAPAVIStats@12@XZ";
static const char* const GALAXY_EXPORT_USER         = "?User@api@galaxy@@YAPAVIUser@12@XZ";
static const char* const GALAXY_EXPORT_GET_ERROR    = "?GetError@api@galaxy@@YAPBVIError@12@XZ";

/******************************************************************************/
/// Forward declarations matching Galaxy SDK binary layout.
/// We mirror the struct/class layouts without including the Galaxy headers
/// to avoid MinGW/MSVC header incompatibilities while still being able to
/// call through the vtable.

/// InitOptions — POD struct matching Galaxy SDK layout (32-bit)
struct GogInitOptions
{
    const char* clientID;
    const char* clientSecret;
    const char* configFilePath;
    const char* storagePath;
    void* galaxyAllocator;        ///< GalaxyAllocator* (unused, NULL)
    void* galaxyThreadFactory;    ///< IGalaxyThreadFactory* (unused, NULL)
    const char* host;
    uint16_t port;
};

/// IError — returned by GetError(), has vtable with GetName/GetMsg/GetType
struct GogIError
{
    void** vtable;
};

/// IUser — returned by User(), we need SignInGalaxy and SignedIn
struct GogIUser
{
    void** vtable;
};

/// IStats — returned by Stats(), we need achievement methods
struct GogIStats
{
    void** vtable;
};

/// GalaxyID — 64-bit value type, default-constructed is 0
struct GogGalaxyID
{
    uint64_t value;
};

/******************************************************************************/
/// Function pointer typedefs for Galaxy DLL exports
typedef void   (__cdecl *GogInitFunc)(const GogInitOptions& initOptions);
typedef void   (__cdecl *GogShutdownFunc)(void);
typedef void   (__cdecl *GogProcessDataFunc)(void);
typedef GogIStats*  (__cdecl *GogStatsFunc)(void);
typedef GogIUser*   (__cdecl *GogUserFunc)(void);
typedef GogIError*  (__cdecl *GogGetErrorFunc)(void);

/// Type punning unions for safe FARPROC → function pointer conversion
/// (avoids -Werror=cast-function-type with MinGW)
union FarProcInit        { FARPROC fp; GogInitFunc fn; };
union FarProcShutdown    { FARPROC fp; GogShutdownFunc fn; };
union FarProcProcessData { FARPROC fp; GogProcessDataFunc fn; };
union FarProcStats       { FARPROC fp; GogStatsFunc fn; };
union FarProcUser        { FARPROC fp; GogUserFunc fn; };
union FarProcGetError    { FARPROC fp; GogGetErrorFunc fn; };

/// IStats vtable indices (0-indexed, slot 0 = destructor):
///  0: ~IStats (destructor)
///  1: RequestUserStatsAndAchievements(GalaxyID, IUserStatsAndAchievementsRetrieveListener*)
///  2: GetStatInt(const char*, GalaxyID)
///  3: GetStatFloat(const char*, GalaxyID)
///  4: SetStatInt(const char*, int32_t)
///  5: SetStatFloat(const char*, float)
///  6: UpdateAvgRateStat(const char*, float, double)
///  7: GetAchievementsNumber()
///  8: GetAchievementName(uint32_t)
///  9: GetAchievementNameCopy(uint32_t, char*, uint32_t)
/// 10: GetAchievement(const char*, bool&, uint32_t&, GalaxyID)
/// 11: SetAchievement(const char*)
/// 12: ClearAchievement(const char*)
/// 13: StoreStatsAndAchievements(IStatsAndAchievementsStoreListener*)

/// IUser vtable indices:
///  0: ~IUser (destructor)
///  1: SignedIn()
///  2: GetGalaxyID()
///  3: SignInCredentials(...)
///  4: SignInToken(...)
///  5: SignInLauncher(...)
///  6: SignInSteam(...)
///  7: SignInGalaxy(bool, uint32_t, IAuthListener*)

/// Vtable call helpers using __thiscall convention (Win32: this in ECX)
/// For MinGW g++, __thiscall is the default for member functions, so we
/// use __attribute__((thiscall)) on the function pointer typedefs.

#ifdef __GNUC__
#define THISCALL __attribute__((thiscall))
#else
#define THISCALL __thiscall
#endif

/// IStats method types
typedef void  (THISCALL *IStats_RequestUserStatsAndAchievements)(GogIStats* self, GogGalaxyID userID, void* listener);
typedef void  (THISCALL *IStats_SetAchievement)(GogIStats* self, const char* name);
typedef void  (THISCALL *IStats_ClearAchievement)(GogIStats* self, const char* name);
typedef void  (THISCALL *IStats_StoreStatsAndAchievements)(GogIStats* self, void* listener);
typedef void  (THISCALL *IStats_GetAchievement)(GogIStats* self, const char* name, bool* unlocked, uint32_t* unlockTime, GogGalaxyID userID);

/// IUser method types
typedef bool  (THISCALL *IUser_SignedIn)(GogIUser* self);
typedef void  (THISCALL *IUser_SignInGalaxy)(GogIUser* self, bool requireOnline, uint32_t timeout, void* listener);

/******************************************************************************/
/// Module state
static HMODULE gog_lib = NULL;
static GogInitFunc         gog_Init = NULL;
static GogShutdownFunc     gog_Shutdown = NULL;
static GogProcessDataFunc  gog_ProcessData = NULL;
static GogStatsFunc        gog_Stats = NULL;
static GogUserFunc         gog_User = NULL;
static GogGetErrorFunc     gog_GetError = NULL;
static bool gog_initialized = false;
static bool gog_stats_ready = false;

/******************************************************************************/
/// Helper: call a vtable method on an IStats*
static inline void* istats_vtable(GogIStats* stats, int index)
{
    return stats->vtable[index];
}

static inline void* iuser_vtable(GogIUser* user, int index)
{
    return user->vtable[index];
}

/******************************************************************************/
/// AchievementBackend implementation

static TbBool gog_backend_init(void)
{
    // Already initialized
    if (gog_initialized)
        return true;

    if (!LbFileExists("Galaxy.dll"))
    {
        WARNLOG("GOG Galaxy: Galaxy.dll not found");
        return false;
    }

    // Load the DLL
    gog_lib = LoadLibraryA("Galaxy.dll");
    if (!gog_lib)
    {
        ERRORLOG("GOG Galaxy: Failed to load Galaxy.dll");
        return false;
    }

    JUSTLOG("GOG Galaxy: Galaxy.dll loaded");

    // Resolve exports using MSVC-mangled names (union type punning for safe FARPROC conversion)
    FarProcInit u_init;               u_init.fp        = GetProcAddress(gog_lib, GALAXY_EXPORT_INIT);
    FarProcShutdown u_shutdown;       u_shutdown.fp     = GetProcAddress(gog_lib, GALAXY_EXPORT_SHUTDOWN);
    FarProcProcessData u_process;     u_process.fp      = GetProcAddress(gog_lib, GALAXY_EXPORT_PROCESS_DATA);
    FarProcStats u_stats;             u_stats.fp        = GetProcAddress(gog_lib, GALAXY_EXPORT_STATS);
    FarProcUser u_user;               u_user.fp         = GetProcAddress(gog_lib, GALAXY_EXPORT_USER);
    FarProcGetError u_error;          u_error.fp        = GetProcAddress(gog_lib, GALAXY_EXPORT_GET_ERROR);

    gog_Init        = u_init.fn;
    gog_Shutdown    = u_shutdown.fn;
    gog_ProcessData = u_process.fn;
    gog_Stats       = u_stats.fn;
    gog_User        = u_user.fn;
    gog_GetError    = u_error.fn;

    if (!gog_Init || !gog_Shutdown || !gog_ProcessData || !gog_Stats || !gog_User)
    {
        ERRORLOG("GOG Galaxy: Failed to resolve required Galaxy SDK exports");
        FreeLibrary(gog_lib);
        gog_lib = NULL;
        return false;
    }

    JUSTLOG("GOG Galaxy: SDK exports resolved");

    // Initialize Galaxy
    GogInitOptions options = {};
    options.clientID       = GOG_CLIENT_ID;
    options.clientSecret   = GOG_CLIENT_SECRET;
    options.configFilePath = ".";
    options.storagePath    = NULL;
    options.galaxyAllocator     = NULL;
    options.galaxyThreadFactory = NULL;
    options.host           = NULL;
    options.port           = 0;

    gog_Init(options);

    // Check for init errors
    if (gog_GetError)
    {
        GogIError* err = gog_GetError();
        if (err != NULL)
        {
            ERRORLOG("GOG Galaxy: Init failed (error object returned)");
            gog_Shutdown();
            FreeLibrary(gog_lib);
            gog_lib = NULL;
            return false;
        }
    }

    JUSTLOG("GOG Galaxy: SDK initialized");

    // Sign in via Galaxy Client
    GogIUser* user = gog_User();
    if (user)
    {
        auto signInGalaxy = (IUser_SignInGalaxy)iuser_vtable(user, 7);
        signInGalaxy(user, false, 15, NULL);

        // Pump data to process sign-in (blocking wait, up to ~2 seconds)
        for (int i = 0; i < 200; i++)
        {
            gog_ProcessData();
            Sleep(10);

            auto signedIn = (IUser_SignedIn)iuser_vtable(user, 1);
            if (signedIn(user))
            {
                JUSTLOG("GOG Galaxy: User signed in");
                break;
            }
        }

        auto signedIn = (IUser_SignedIn)iuser_vtable(user, 1);
        if (!signedIn(user))
        {
            WARNLOG("GOG Galaxy: Sign-in timed out (achievements may still work offline)");
        }
    }

    // Request user stats and achievements
    GogIStats* stats = gog_Stats();
    if (stats)
    {
        GogGalaxyID nullID = {0};
        auto requestStats = (IStats_RequestUserStatsAndAchievements)istats_vtable(stats, 1);
        requestStats(stats, nullID, NULL);

        // Pump data to process stats request
        for (int i = 0; i < 100; i++)
        {
            gog_ProcessData();
            Sleep(10);
        }

        gog_stats_ready = true;
        JUSTLOG("GOG Galaxy: Stats and achievements requested");
    }

    gog_initialized = true;
    return true;
}

static void gog_backend_shutdown(void)
{
    if (!gog_initialized)
        return;

    JUSTLOG("GOG Galaxy: Shutting down");

    if (gog_Shutdown)
    {
        gog_Shutdown();
    }

    gog_Init = NULL;
    gog_Shutdown = NULL;
    gog_ProcessData = NULL;
    gog_Stats = NULL;
    gog_User = NULL;
    gog_GetError = NULL;

    if (gog_lib)
    {
        FreeLibrary(gog_lib);
        gog_lib = NULL;
    }

    gog_initialized = false;
    gog_stats_ready = false;
}

static TbBool gog_backend_unlock(const char* achievement_id)
{
    if (!gog_initialized || !gog_stats_ready)
        return false;

    GogIStats* stats = gog_Stats();
    if (!stats)
        return false;

    // SetAchievement (vtable index 11)
    auto setAchievement = (IStats_SetAchievement)istats_vtable(stats, 11);
    setAchievement(stats, achievement_id);

    // Check for error
    if (gog_GetError)
    {
        GogIError* err = gog_GetError();
        if (err != NULL)
        {
            WARNLOG("GOG Galaxy: SetAchievement failed for '%s'", achievement_id);
            return false;
        }
    }

    // StoreStatsAndAchievements (vtable index 13)
    auto storeStats = (IStats_StoreStatsAndAchievements)istats_vtable(stats, 13);
    storeStats(stats, NULL);

    SYNCDBG(8, "GOG Galaxy: Achievement unlocked: %s", achievement_id);
    return true;
}

static TbBool gog_backend_is_unlocked(const char* achievement_id)
{
    if (!gog_initialized || !gog_stats_ready)
        return false;

    GogIStats* stats = gog_Stats();
    if (!stats)
        return false;

    // GetAchievement (vtable index 10)
    bool unlocked = false;
    uint32_t unlockTime = 0;
    GogGalaxyID nullID = {0};
    auto getAchievement = (IStats_GetAchievement)istats_vtable(stats, 10);
    getAchievement(stats, achievement_id, &unlocked, &unlockTime, nullID);

    return unlocked ? true : false;
}

static void gog_backend_set_progress(const char* achievement_id, float progress)
{
    // GOG Galaxy doesn't natively support achievement progress percentages.
    // Progress is tracked locally; auto-unlock happens at 100% in achievement_api.c
    (void)achievement_id;
    (void)progress;
}

static float gog_backend_get_progress(const char* achievement_id)
{
    // Check if unlocked on GOG side
    if (gog_backend_is_unlocked(achievement_id))
        return 1.0f;
    return 0.0f;
}

static void gog_backend_sync(void)
{
    if (!gog_initialized)
        return;

    // ProcessData pumps Galaxy SDK callbacks
    if (gog_ProcessData)
    {
        gog_ProcessData();
    }
}

/******************************************************************************/
/// The GOG Galaxy achievement backend definition
static struct AchievementBackend gog_galaxy_backend = {
    "GOG Galaxy",            // name
    AchPlat_GOG,             // platform_type
    gog_backend_init,        // init
    gog_backend_shutdown,    // shutdown
    gog_backend_unlock,      // unlock
    gog_backend_is_unlocked, // is_unlocked
    gog_backend_set_progress,// set_progress
    gog_backend_get_progress,// get_progress
    gog_backend_sync,        // sync
};

#endif // _WIN32

/******************************************************************************/
/// Public API

int gog_galaxy_init(void)
{
#ifndef _WIN32
    return -1;
#else
    if (!LbFileExists("Galaxy.dll"))
    {
        return 1;
    }

    // Register the GOG Galaxy backend with the achievement system
    if (!achievements_register_backend(&gog_galaxy_backend))
    {
        ERRORLOG("GOG Galaxy: Failed to register achievement backend");
        return 1;
    }

    JUSTLOG("GOG Galaxy: Backend registered");
    return 0;
#endif
}

void gog_galaxy_shutdown(void)
{
#ifdef _WIN32
    gog_backend_shutdown();
#endif
}
