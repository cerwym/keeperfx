/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file gui_bridge.cpp
 *     C-bridge implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/gui_bridge.h"
#include "gui/SceneManager.h"
#include "gui/ParchmentScene.h"
#include "player_data.h"        // PlayerInfo, get_my_player, is_my_player
#include "config_settings.h"    // settings, save_settings
#include "engine_camera.h"      // CamIV_*
#include "local_camera.h"       // set_local_camera_destination

extern "C" {

void GUIBridge_Update(float global_dt)
{
    SceneManager::get().update(global_dt);
}

void GUIBridge_Draw(void)
{
    SceneManager::get().draw();
}

int SceneManager_IsZoomBoxHandled(void)
{
    // Returns true when the top scene declares that it handles (and draws)
    // the zoom box itself, so draw_zoom_box() in gui_parchment.c should skip
    // its legacy body.
    IScene* top = SceneManager::get().topScene();
    return (top && top->handlesZoomBox()) ? 1 : 0;
}

void GUIBridge_DrawParchmentView(void)
{
    SceneManager& sm = SceneManager::get();

    // Idempotent: push ParchmentScene only if it isn't already on top.
    IScene* top = sm.topScene();
    if (!top || !top->handlesZoomBox())
        sm.resetTo(std::make_unique<ParchmentScene>());

    // update() is driven once per frame by GUIBridge_Update() in gameplay_loop_draw().
    // Just draw — do not call update() here to avoid double-tick.
    sm.draw();
}

void GUIBridge_LeaveParchmentView(void)
{
    // Pop ParchmentScene when the engine transitions away from PVM_ParchmentView.
    // Guard: only pop if the top scene is actually a ParchmentScene (i.e.
    // handlesZoomBox) so we never accidentally pop an unrelated scene.
    SceneManager& sm = SceneManager::get();
    IScene* top = sm.topScene();
    if (top && top->handlesZoomBox())
        sm.pop();
}

int GUIBridge_HandleKeyDown(int key)
{
    InputEvent ev;
    ev.type = InputEvent::KeyDown;
    ev.key  = key;
    return SceneManager::get().handleInput(ev) ? 1 : 0;
}

int GUIBridge_HandleMouseDown(int x, int y, int button)
{
    InputEvent ev;
    ev.type   = InputEvent::MouseDown;
    ev.x      = x;
    ev.y      = y;
    ev.button = button;
    return SceneManager::get().handleInput(ev) ? 1 : 0;
}

void GUIBridge_SetMinimapZoom(unsigned short new_zoom)
{
    // Clamp to valid range
    if (new_zoom < 128)  new_zoom = 128;
    if (new_zoom > 2048) new_zoom = 2048;

    struct PlayerInfo* player = get_my_player();
    if (!player) return;

    player->minimap_zoom = new_zoom;
    settings.minimap_zoom = new_zoom;
    save_settings();

    // Mirror into ClientViewState immediately so the scene sees the new zoom
    // without waiting for the next syncFromLegacy() call.
    SceneManager::get().viewState().minimap_zoom = new_zoom;
}

void GUIBridge_SetMapRotation(int angle)
{
    struct PlayerInfo* player = get_my_player();
    if (!player) return;

    player->cameras[CamIV_Parchment].rotation_angle_x = angle;
    player->cameras[CamIV_FrontView].rotation_angle_x = angle;
    player->cameras[CamIV_Isometric].rotation_angle_x = angle;
    set_local_camera_destination(player);

    // Mirror into ClientViewState immediately.
    ClientViewState& view = SceneManager::get().viewState();
    view.cameras[CamIV_Parchment].rotation_angle_x = angle;
    view.cameras[CamIV_FrontView].rotation_angle_x = angle;
    view.cameras[CamIV_Isometric].rotation_angle_x = angle;
}

} // extern "C"
