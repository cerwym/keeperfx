/******************************************************************************/
/** @file SteamBackend.h
 *     Steam achievement backend header.
 * @par Purpose:
 *     Provides the AchievementBackend implementation for Steam.
 *     Dynamically loads steam_api.dll at runtime.
 *     Steam init is lightweight and main-thread safe (needs_worker_thread=false).
 * @author   KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef STEAM_BACKEND_H
#define STEAM_BACKEND_H

#include "../achievement/achievement_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the Steam backend with the IntegrationManager.
 * Checks for steam_api.dll and steam_appid.txt; if not present, does nothing.
 * @return 0 on success, 1 on failure/skip, -1 if not on Windows.
 */
int steam_backend_register(void);

#ifdef __cplusplus
}
#endif

#endif // STEAM_BACKEND_H
