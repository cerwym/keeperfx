/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ClientViewState.h
 *     Local-only view state for the client player.
 *
 *     This struct holds all per-player view data that belongs to the LOCAL
 *     CLIENT only and must NEVER be:
 *       - serialised into savegames
 *       - transmitted over the network
 *       - stored in PlayerInfo
 *
 *     Owned as a value member of SceneManager.  Populated once per frame via
 *     syncFromLegacy() during the migration period while cameras[] still live
 *     in PlayerInfo.  Once the migration is complete, this becomes the sole
 *     source of truth for all view state.
 *
 * @note minimap_zoom is currently synced via PckA_SetMinimapConf (wrong).
 *       It is mirrored here so new code reads from ClientViewState.  The
 *       packet path will be retired in Phase 3.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "engine_camera.h"  // struct Camera, CamIV_* enum

struct PlayerInfo;          // forward-declare; do not pull in player_data.h

struct ClientViewState
{
    /// Local copies of the four player cameras (isometric, first-person,
    /// parchment, front-view).  Indexed by CamIV_* enum values.
    Camera cameras[CamIV_EndList];

    /// Active camera — points into cameras[].
    /// Mirrors player->acamera during the migration period.
    Camera* acamera = nullptr;

    /// Cached zoom level for the isometric dungeon camera.
    int dungeon_camera_zoom = 0;

    /// Number of tiles shown in the parchment zoom box.
    /// Mirrors player->minimap_zoom; packet sync of this value will be
    /// retired in Phase 3 (it is a display preference, not game state).
    unsigned short minimap_zoom = 0;

    /// Viewport rect used by the engine window (isometric render area).
    short engine_window_x      = 0;
    short engine_window_y      = 0;
    short engine_window_width  = 0;
    short engine_window_height = 0;

    /// Map dimensions in slabs, used for zoom-box tile clamping.
    /// Mirrors game.map_tiles_x / game.map_tiles_y.
    int16_t map_tiles_x = 0;
    int16_t map_tiles_y = 0;

    // -------------------------------------------------------------------------
    // Migration bridge — call once per frame at the top of SceneManager::update.
    // Copies fields from PlayerInfo into this struct.
    // Remove when cameras[] and minimap_zoom are fully migrated out of PlayerInfo.
    // -------------------------------------------------------------------------
    void syncFromLegacy(const PlayerInfo* player);
};

#endif // __cplusplus
