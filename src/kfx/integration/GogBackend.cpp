/******************************************************************************/
/** @file GogBackend.cpp
 *     GOG Galaxy SDK integration for KeeperFX achievements.
 * @par Purpose:
 *     Dynamically loads Galaxy.dll at runtime and provides achievement
 *     operations via the AchievementBackend interface. All functions are
 *     called on the IntegrationManager's worker thread (needs_worker_thread=true).
 * @par Comment:
 *     Uses MSVC-mangled export names with GetProcAddress since Galaxy.dll
 *     is compiled with MSVC. The C++ vtable layout is binary-compatible
 *     between MSVC and MinGW on Win32 for single-inheritance classes.
 *     This file is only compiled on Windows when GOG support is enabled.
 *     Claude Sonnet contributed the initial implementation. Thanks, Claude,
 *     because JFC, the Galaxy SDK is a nightmare to work with.
 *
 * @author   Peter Lockett (Via Claude Sonnet) & KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "GogBackend.h"
#include "IntegrationManager.h"
#include "../achievement/achievement_api.h"
#include "../../bflib_basics.h"
#include "../../bflib_fileio.h"
#include "../../post_inc.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setjmp.h>
#include <cstring>
#include "../../keeperfx.hpp"
#endif

/******************************************************************************/
// GOG Galaxy Client Credentials (public — embedded in all GOG game binaries)
#define GOG_CLIENT_ID     "58850072615442688"
#define GOG_CLIENT_SECRET "972283970fbaad55d7de7b8a34212598913d741d1314952eeedc054cd1df4434"

/******************************************************************************/
#ifdef _WIN32

// Claude helped with this shitreck of a file. Thanks, Claude!
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

struct GogInitOptions
{
    const char* clientID;
    const char* clientSecret;
    const char* configFilePath;
    const char* storagePath;
    void* galaxyAllocator;
    void* galaxyThreadFactory;
    const char* host;
    uint16_t port;
};

struct GogIError  { void** vtable; };
struct GogIUser   { void** vtable; };
struct GogIStats  { void** vtable; };
struct GogGalaxyID { uint64_t value; };

/******************************************************************************/
/// Function pointer typedefs for Galaxy DLL exports
typedef void   (__cdecl *GogInitFunc)(const GogInitOptions& initOptions);
typedef void   (__cdecl *GogShutdownFunc)(void);
typedef void   (__cdecl *GogProcessDataFunc)(void);
typedef GogIStats*  (__cdecl *GogStatsFunc)(void);
typedef GogIUser*   (__cdecl *GogUserFunc)(void);
typedef GogIError*  (__cdecl *GogGetErrorFunc)(void);

/// Type punning unions for safe FARPROC → function pointer conversion
union FarProcInit        { FARPROC fp; GogInitFunc fn; };
union FarProcShutdown    { FARPROC fp; GogShutdownFunc fn; };
union FarProcProcessData { FARPROC fp; GogProcessDataFunc fn; };
union FarProcStats       { FARPROC fp; GogStatsFunc fn; };
union FarProcUser        { FARPROC fp; GogUserFunc fn; };
union FarProcGetError    { FARPROC fp; GogGetErrorFunc fn; };

/// IStats vtable indices (0-indexed, slot 0 = destructor):
///  1: RequestUserStatsAndAchievements   7: GetAchievementsNumber
///  8: GetAchievementName  10: GetAchievement  11: SetAchievement
/// 12: ClearAchievement    13: StoreStatsAndAchievements

/// IUser vtable indices:
///  1: SignedIn  7: SignInGalaxy

#ifdef __GNUC__
#define THISCALL __attribute__((thiscall))
#else
#define THISCALL __thiscall
#endif

typedef void  (THISCALL *IStats_RequestUserStatsAndAchievements)(GogIStats* self, GogGalaxyID userID, void* listener);
typedef void  (THISCALL *IStats_SetAchievement)(GogIStats* self, const char* name);
typedef void  (THISCALL *IStats_ClearAchievement)(GogIStats* self, const char* name);
typedef void  (THISCALL *IStats_StoreStatsAndAchievements)(GogIStats* self, void* listener);
typedef void  (THISCALL *IStats_GetAchievement)(GogIStats* self, const char* name, bool* unlocked, uint32_t* unlockTime, GogGalaxyID userID);
typedef bool  (THISCALL *IUser_SignedIn)(GogIUser* self);
typedef void  (THISCALL *IUser_SignInGalaxy)(GogIUser* self, bool requireOnline, uint32_t timeout, void* listener);
typedef const char* (THISCALL *IError_GetName)(GogIError* self);
typedef const char* (THISCALL *IError_GetMsg)(GogIError* self);

/******************************************************************************/
/// Module state — all accessed only from the worker thread
static HMODULE gog_lib = NULL;
static GogInitFunc         gog_Init = NULL;
static GogShutdownFunc     gog_Shutdown = NULL;
static GogProcessDataFunc  gog_ProcessData = NULL;
static GogStatsFunc        gog_Stats = NULL;
static GogUserFunc         gog_User = NULL;
static GogGetErrorFunc     gog_GetError = NULL;
static bool gog_initialized = false;
static bool gog_stats_ready = false;

static inline void* istats_vtable(GogIStats* stats, int index) { return stats->vtable[index]; }
static inline void* iuser_vtable(GogIUser* user, int index) { return user->vtable[index]; }

/******************************************************************************/
/// SEH wrapper for Galaxy SDK calls.
/// Galaxy.dll (MSVC) throws C++ exceptions (0xe06d7363) on errors.
/// MinGW can't catch these with try/catch, so we use a scoped VEH + longjmp.
/// The thrown object is an IError subclass — we extract its name and message
/// from the MSVC exception record BEFORE longjmp.
static jmp_buf gog_seh_jmp;
static volatile bool gog_seh_active = false;
static char gog_caught_err_name[128];
static char gog_caught_err_msg[256];

static LONG CALLBACK gog_seh_handler(PEXCEPTION_POINTERS ep)
{
    if (gog_seh_active && ep->ExceptionRecord->ExceptionCode == 0xe06d7363)
    {
        gog_caught_err_name[0] = '\0';
        gog_caught_err_msg[0] = '\0';

        if (ep->ExceptionRecord->NumberParameters >= 3
            && ep->ExceptionRecord->ExceptionInformation[1] != 0)
        {
            GogIError* err = (GogIError*)(ep->ExceptionRecord->ExceptionInformation[1]);
            if (err->vtable)
            {
                auto getName = (IError_GetName)err->vtable[1];
                auto getMsg  = (IError_GetMsg)err->vtable[2];
                const char* n = getName ? getName(err) : NULL;
                const char* m = getMsg  ? getMsg(err)  : NULL;
                if (n) std::strncpy(gog_caught_err_name, n, sizeof(gog_caught_err_name) - 1);
                if (m) std::strncpy(gog_caught_err_msg,  m, sizeof(gog_caught_err_msg)  - 1);
            }
        }

        gog_seh_active = false;
        longjmp(gog_seh_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool gog_log_last_error(const char* context)
{
    if (gog_caught_err_name[0] != '\0' || gog_caught_err_msg[0] != '\0')
    {
        WARNLOG("GOG Galaxy: %s — %s: %s", context,
                gog_caught_err_name[0] ? gog_caught_err_name : "Unknown",
                gog_caught_err_msg[0]  ? gog_caught_err_msg  : "(no message)");
        gog_caught_err_name[0] = '\0';
        gog_caught_err_msg[0]  = '\0';
        return true;
    }

    if (!gog_GetError)
        return false;
    GogIError* err = gog_GetError();
    if (err == NULL)
        return false;

    const char* name = "?";
    const char* msg = "?";
    if (err->vtable)
    {
        auto getName = (IError_GetName)err->vtable[1];
        auto getMsg  = (IError_GetMsg)err->vtable[2];
        if (getName) name = getName(err);
        if (getMsg)  msg  = getMsg(err);
    }
    WARNLOG("GOG Galaxy: %s — %s: %s", context, name, msg);
    return true;
}

#define GOG_SAFE_CALL(call, context) \
    do { \
        void *_veh = AddVectoredExceptionHandler(1, gog_seh_handler); \
        gog_seh_active = true; \
        if (setjmp(gog_seh_jmp) == 0) { \
            call; \
            gog_seh_active = false; \
            RemoveVectoredExceptionHandler(_veh); \
            if (gog_log_last_error(context)) \
                return false; \
        } else { \
            RemoveVectoredExceptionHandler(_veh); \
            gog_log_last_error(context); \
            return false; \
        } \
    } while(0)

/******************************************************************************/
/// AchievementBackend implementation.
/// All functions are called on the IntegrationManager worker thread.

static TbBool gog_backend_init(void)
{
    if (gog_initialized)
        return true;

    gog_lib = LoadLibraryA("Galaxy.dll");
    if (!gog_lib)
    {
        ERRORLOG("GOG Galaxy: Failed to load Galaxy.dll");
        return false;
    }

    JUSTLOG("GOG Galaxy: Galaxy.dll loaded");

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

    GogInitOptions options = {};
    options.clientID       = GOG_CLIENT_ID;
    options.clientSecret   = GOG_CLIENT_SECRET;
    options.configFilePath = ".";

    gog_Init(options);

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
            WARNLOG("GOG Galaxy: Sign-in timed out (achievements may still work offline)");
    }

    // Request user stats and achievements
    GogIStats* stats = gog_Stats();
    if (stats)
    {
        GogGalaxyID nullID = {0};
        auto requestStats = (IStats_RequestUserStatsAndAchievements)istats_vtable(stats, 1);
        requestStats(stats, nullID, NULL);

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
    if (gog_Shutdown) gog_Shutdown();

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

    const char* gog_key = achievement_get_platform_id(achievement_id, "gog");

    GogIStats* stats = gog_Stats();
    if (!stats) return false;

    auto setAchievement = (IStats_SetAchievement)istats_vtable(stats, 11);
    GOG_SAFE_CALL(setAchievement(stats, gog_key), "SetAchievement");

    auto storeStats = (IStats_StoreStatsAndAchievements)istats_vtable(stats, 13);
    GOG_SAFE_CALL(storeStats(stats, NULL), "StoreStatsAndAchievements");

    SYNCDBG(8, "GOG Galaxy: Achievement unlocked: %s (key: %s)", achievement_id, gog_key);
    return true;
}

static TbBool gog_backend_is_unlocked(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    return (ach != NULL && ach->unlocked) ? true : false;
}

static TbBool gog_backend_set_progress(const char* achievement_id, float progress)
{
    (void)achievement_id;
    (void)progress;
    return true;
}

static float gog_backend_get_progress(const char* achievement_id)
{
    struct Achievement* ach = achievement_find(achievement_id);
    return (ach != NULL) ? ach->progress : 0.0f;
}

static TbBool gog_backend_clear(const char* achievement_id)
{
    if (!gog_initialized || !gog_stats_ready)
        return false;

    const char* gog_key = achievement_get_platform_id(achievement_id, "gog");

    GogIStats* stats = gog_Stats();
    if (!stats) return false;

    auto clearAchievement = (IStats_ClearAchievement)istats_vtable(stats, 12);
    GOG_SAFE_CALL(clearAchievement(stats, gog_key), "ClearAchievement");

    auto storeStats = (IStats_StoreStatsAndAchievements)istats_vtable(stats, 13);
    GOG_SAFE_CALL(storeStats(stats, NULL), "StoreStatsAndAchievements");

    SYNCDBG(8, "GOG Galaxy: Achievement cleared: %s (key: %s)", achievement_id, gog_key);
    return true;
}

static void gog_backend_sync(void)
{
    if (gog_initialized && gog_ProcessData)
        gog_ProcessData();
}

/******************************************************************************/
static struct AchievementBackend gog_galaxy_backend = {
    "GOG Galaxy",            // name
    AchPlat_GOG,             // platform_type
    gog_backend_init,        // init
    gog_backend_shutdown,    // shutdown
    gog_backend_unlock,      // unlock
    gog_backend_is_unlocked, // is_unlocked
    gog_backend_set_progress,// set_progress
    gog_backend_get_progress,// get_progress
    gog_backend_clear,       // clear
    gog_backend_sync,        // sync
    true,                    // needs_worker_thread
};

#endif // _WIN32

/******************************************************************************/
int gog_backend_register(void)
{
#ifndef _WIN32
    return -1;
#else
    if (!LbFileExists("Galaxy.dll"))
        return 1;

    integration_manager_register_backend(&gog_galaxy_backend);
    JUSTLOG("GOG Galaxy: Backend registered with IntegrationManager");
    return 0;
#endif
}
