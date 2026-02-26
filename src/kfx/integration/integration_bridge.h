/******************************************************************************/
/** @file integration_bridge.h
 *     C-callable bridge to the IntegrationManager.
 *     This header is safe to include from pure C files.
 */
/******************************************************************************/
#ifndef INTEGRATION_BRIDGE_H
#define INTEGRATION_BRIDGE_H

#include "../achievement/achievement_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void integration_manager_register_backend(struct AchievementBackend* backend);
void integration_manager_init(void);
void integration_manager_shutdown(void);
void integration_manager_unlock(const char* achievement_id);
void integration_manager_clear(const char* achievement_id);
void integration_manager_set_progress(const char* achievement_id, float progress);
void integration_manager_sync(void);

#ifdef __cplusplus
}
#endif

#endif // INTEGRATION_BRIDGE_H
