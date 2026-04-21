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

#include <cstring>          // memcpy

void ClientViewState::syncFromLegacy(const PlayerInfo* player)
{
    if (!player)
        return;

    // Copy all four cameras from PlayerInfo.
    // The acamera pointer inside player points into player->cameras[]; remap it
    // to point into our local cameras[] array instead.
    memcpy(cameras, player->cameras, sizeof(cameras));

    if (player->acamera)
    {
        // Determine which slot acamera points to and mirror that offset.
        ptrdiff_t idx = player->acamera - &player->cameras[0];
        if (idx >= 0 && idx < CamIV_EndList)
            acamera = &cameras[idx];
        else
            acamera = &cameras[CamIV_Isometric];
    }
    else
    {
        acamera = &cameras[CamIV_Isometric];
    }

    dungeon_camera_zoom  = player->dungeon_camera_zoom;
    minimap_zoom         = player->minimap_zoom;
    engine_window_x      = player->engine_window_x;
    engine_window_y      = player->engine_window_y;
    engine_window_width  = player->engine_window_width;
    engine_window_height = player->engine_window_height;

    map_tiles_x = game.map_tiles_x;
    map_tiles_y = game.map_tiles_y;
}
