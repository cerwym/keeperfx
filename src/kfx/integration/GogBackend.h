/******************************************************************************/
/** @file GogBackend.h
 *     GOG Galaxy achievement backend header.
 * @par Purpose:
 *     Provides the AchievementBackend implementation for GOG Galaxy.
 *     Dynamically loads Galaxy.dll at runtime. All SDK calls happen on
 *     the IntegrationManager's worker thread (needs_worker_thread=true).
 * @author   Peter Lockett (Via Claude Sonnet) & KeeperFX Team
 * @date     25 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef GOG_BACKEND_H
#define GOG_BACKEND_H

#include "../achievement/achievement_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the GOG Galaxy backend with the IntegrationManager.
 * Checks for Galaxy.dll; if not present, does nothing.
 * @return 0 on success, 1 on failure/skip, -1 if not on Windows.
 */
int gog_backend_register(void);

#ifdef __cplusplus
}
#endif

#endif // GOG_BACKEND_H
