/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ClientViewState.cpp
 *     ClientViewState::syncFromLegacy() — migration bridge.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/ClientViewState.h"
#include "player_data.h"    // PlayerInfo, struct Camera
#include "game_legacy.h"    // game.map_tiles_x/y
#include "kfx/engine/cameras.h"

#include <cstring>          // memcpy

void ClientViewState::syncFromLegacy(const PlayerInfo* player)
{
    if (!player)
        return;

    // Copy all four cameras from the opaque cameras module.
    int pi = player->id_number;
    for (int i = 0; i < CamIV_EndList; i++)
        cameras[i] = *camera_get_slot(pi, i);

    int active_idx = camera_get_active_idx(pi);
    if (active_idx >= 0 && active_idx < CamIV_EndList)
        acamera = &cameras[active_idx];
    else
        acamera = &cameras[CamIV_Isometric];

    dungeon_camera_zoom  = player->dungeon_camera_zoom;
    minimap_zoom         = player->minimap_zoom;
    engine_window_x      = player->engine_window_x;
    engine_window_y      = player->engine_window_y;
    engine_window_width  = player->engine_window_width;
    engine_window_height = player->engine_window_height;

    map_tiles_x = game.map_tiles_x;
    map_tiles_y = game.map_tiles_y;
}
