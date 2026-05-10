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
#include "gui/ZoomBoxView.h"
#include "gui/DrawContext.h"
#include "player_data.h"        // PlayerInfo, get_my_player, is_my_player
#include "config_settings.h"    // settings, save_settings
#include "engine_camera.h"      // CamIV_*
#include "kfx/engine/cameras.h"
#include "local_camera.h"       // set_local_camera_destination
#include "bflib_video.h"        // scale_value_for_resolution, LbScreenWidth/Height, pixel_size
#include "bflib_planar.h"       // TbRect
#include "map_data.h"           // STL_PER_SLB, subtile_has_slab
#include "game_legacy.h"        // game, GameState (hand_over_subtile_x/y, small_map_state)
#include "thing_data.h"         // thing_is_invalid
#include "renderer/RendererSettings.h" // RENDERER_ZBM_ISOMETRIC
#include "kjm_input.h"          // left/right_button_released, GetMouseX/Y
#include "power_hand.h"         // get_first_thing_in_power_hand, can_place_thing_here
#include "packets.h"            // set_players_packet_action, PckA_UsePwrHandDrop, PckA_BookmarkLoad

#include <memory>               // unique_ptr

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

    camera_get_slot(player->id_number, CamIV_Parchment)->rotation_angle_x = angle;
    camera_get_slot(player->id_number, CamIV_FrontView)->rotation_angle_x = angle;
    camera_get_slot(player->id_number, CamIV_Isometric)->rotation_angle_x = angle;
    set_local_camera_destination(player);

    // Mirror into ClientViewState immediately.
    ClientViewState& view = SceneManager::get().viewState();
    view.cameras[CamIV_Parchment].rotation_angle_x = angle;
    view.cameras[CamIV_FrontView].rotation_angle_x = angle;
    view.cameras[CamIV_Isometric].rotation_angle_x = angle;
}

// ---------------------------------------------------------------------------
// Game-view PiP — Ctrl+1 / Ctrl+2, stacked vertically in the top-right corner
// ---------------------------------------------------------------------------

namespace {

struct PiPSlot {
    bool                         enabled = false;
    int                          stl_x   = 0;
    int                          stl_y   = 0;
    std::unique_ptr<ZoomBoxView> zoom_box;
};

static PiPSlot g_pips[2];
static bool    g_pip_mouse_over = false; ///< True for the duration of a frame when the cursor is inside a PiP window.

static void togglePiP(int idx)
{
    PiPSlot& slot = g_pips[idx];
    if (slot.enabled)
    {
        slot.enabled = false;
        slot.zoom_box.reset();
        return;
    }

    const PlayerInfo* player = get_my_player();
    if (!player)
        return;

    // Centre the 9-tile box on the cursor position at the moment of activation.
    // Snapshot the current main camera zoom so the PiP is independent of future
    // main camera zoom changes.
    slot.stl_x    = player->cursor_subtile_x - 4;
    slot.stl_y    = player->cursor_subtile_y - 4;
    slot.zoom_box = std::make_unique<ZoomBoxView>(RENDERER_ZBM_ISOMETRIC);
    const Camera* iso_cam = camera_get_slot(player->id_number, CamIV_Isometric);
    slot.zoom_box->setFixedZoom(get_camera_zoom(const_cast<Camera*>(iso_cam)));
    slot.zoom_box->setFixedRotation(iso_cam->rotation_angle_x);
    slot.enabled  = true;
}

} // anonymous namespace

void GUIBridge_ToggleGameViewPiP1(void) { togglePiP(0); }
void GUIBridge_ToggleGameViewPiP2(void) { togglePiP(1); }

void GUIBridge_DrawGameViewPiP(void)
{
    const PlayerInfo* player = get_my_player();
    if (!player)
        return;

    const int draw_tiles = 9;
    const int subtile_sz = (int)scale_value_for_resolution(12);
    const int box_px     = draw_tiles * subtile_sz;
    const int margin     = (int)scale_value_for_resolution(8);  // outer + between-box gap

    // Right-edge of engine window in screen pixels.
    const int right_edge = (player->engine_window_x + player->engine_window_width)  * (int)pixel_size;
    const int top_edge   =  player->engine_window_y                                  * (int)pixel_size;

    const int scr_x = right_edge - box_px - margin;
    int       scr_y = top_edge   + margin;

    DrawContext            ctx  = DrawContext::capture();
    const ClientViewState& view = SceneManager::get().viewState();

    for (int i = 0; i < 2; ++i)
    {
        PiPSlot& slot = g_pips[i];
        if (!slot.enabled || !slot.zoom_box)
            continue;

        TbRect rect;
        rect.left   = scr_x;
        rect.top    = scr_y;
        rect.right  = scr_x + box_px;
        rect.bottom = scr_y + box_px;

        slot.zoom_box->draw(ctx, view, player,
                            rect,
                            slot.stl_x, slot.stl_y,
                            draw_tiles, draw_tiles,
                            subtile_sz);

        scr_y += box_px + margin;
    }
}

int GUIBridge_HandleGameViewPiPInput(void)
{
    g_pip_mouse_over = false; // Reset each call; set below if mouse is inside a PiP.

    PlayerInfo* player = get_my_player();
    if (!player)
        return 0;

    // Must match the layout in GUIBridge_DrawGameViewPiP() exactly.
    const int draw_tiles = 9;
    const int subtile_sz = (int)scale_value_for_resolution(12);
    const int box_px     = draw_tiles * subtile_sz;
    const int margin     = (int)scale_value_for_resolution(8);

    const int right_edge = (player->engine_window_x + player->engine_window_width)  * (int)pixel_size;
    const int top_edge   =  player->engine_window_y                                  * (int)pixel_size;

    const int scr_x = right_edge - box_px - margin;
    int       scr_y = top_edge   + margin;

    const long mx = GetMouseX();
    const long my = GetMouseY();

    for (int i = 0; i < 2; ++i, scr_y += box_px + margin)
    {
        PiPSlot& slot = g_pips[i];
        if (!slot.enabled || !slot.zoom_box)
            continue;

        // Hit-test: is the mouse inside this PiP rect?
        if (mx < scr_x || mx >= scr_x + box_px ||
            my < scr_y || my >= scr_y + box_px)
            continue;

        // Convert pixel offset within the PiP box to a map subtile.
        const int local_x = (int)(mx - scr_x);
        const int local_y = (int)(my - scr_y);
        const int stl_x   = slot.stl_x + local_x / subtile_sz;
        const int stl_y   = slot.stl_y + local_y / subtile_sz;

        game.hand_over_subtile_x = stl_x;
        game.hand_over_subtile_y = stl_y;
        g_pip_mouse_over = true;

        // Right-button: hover highlight + drop on release.
        struct Thing* held = get_first_thing_in_power_hand(player);
        if (!thing_is_invalid(held))
        {
            if (can_place_thing_here(held, stl_x, stl_y, player->id_number))
                game.small_map_state = 1;
        }
        if (right_button_clicked)
            right_button_clicked = 0;
        if (right_button_released)
        {
            right_button_released = 0;
            if (subtile_has_slab(stl_x, stl_y))
            {
                set_players_packet_action(player, PckA_UsePwrHandDrop, stl_x, stl_y, 0, 0);
                return 1;
            }
        }

        // Left-button: navigate main camera to this tile.
        if (left_button_released)
        {
            left_button_released = 0;
            if (subtile_has_slab(stl_x, stl_y))
            {
                set_players_packet_action(player, PckA_BookmarkLoad, stl_x, stl_y, 0, 0);
                return 1;
            }
        }

        return 1; // consumed (mouse is inside a PiP)
    }

    return 0;
}

int GUIBridge_IsMouseOverPiP(void)
{
    return g_pip_mouse_over ? 1 : 0;
}

} // extern "C"
