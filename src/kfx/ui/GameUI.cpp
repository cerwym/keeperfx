/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file GameUI.cpp
 *     Owner/orchestrator of the composed in-game UI.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/ui/GameUI.h"

#include "game_legacy.h"          /* game, GOF_ShowGui, flag_is_set */
#include "frontmenu_ingame_tabs.h" /* draw_whole_status_panel */
#include "frontend.h"              /* draw_gui */
#include "engine_redraw.h"         /* draw_overlay_compass */
#include "gui_msgs.h"               /* message_draw */
#include "gui_tooltips.h"           /* draw_tooltip */
#include "power_hand.h"             /* draw_power_hand */
#include "gui_boxmenu.h"            /* gui_draw_all_boxes */
#include "renderer/RendMenuOverlay.h" /* RendMenu_Draw, RendMenu_IsOpen */
#include "ui_init.h"                 /* should_render_ui */
#include "player_data.h"             /* struct PlayerInfo, PVT_MapScreen/MapFadeIn/MapFadeOut */
#include "gui/SceneManager.h"        /* SceneManager::get().topScene() */
#include "gui/IScene.h"              /* IScene::wantsGameUI() */

#include "post_inc.h"

/******************************************************************************/

GameUI& GameUI::Get()
{
    static GameUI s_instance;
    return s_instance;
}

bool GameUI::IsActiveForCurrentView(const struct PlayerInfo* player) const
{
    if (!player)
        return false;
    if (player->view_type == PVT_MapScreen
        || player->view_type == PVT_MapFadeIn
        || player->view_type == PVT_MapFadeOut)
        return false;
    IScene* top = SceneManager::get().topScene();
    if (top && !top->wantsGameUI())
        return false;
    return true;
}

void GameUI::DrawFrame(struct PlayerInfo* player)
{
    if (!IsActiveForCurrentView(player))
        return;

    // The sidebar is a single composed unit built from several overlapping draw
    // passes — the panel chrome and slot backgrounds go down first, then the
    // room/spell/creature icons fill those slots, then the minimap terrain and
    // compass button labels render on top. In the original software renderer
    // these were sequential blits to the same pixel buffer; here they submit IR
    // commands that composite in the same order into the GameUI layer.
    // 
    // Sidebar is a heap of shit, todo : composite it
    //
    // When a full-screen overlay menu is active (RendMenu_IsOpen()), that menu
    // covers the game view entirely, so the sidebar's content draws (icons,
    // compass overlay, messages, tooltip) are suppressed — they're irrelevant
    // until the overlay closes. The panel background is still submitted to avoid
    // a visual gap on transparent menu edges.
    TbBool menu_open    = RendMenu_IsOpen();
    TbBool show_sidebar = flag_is_set(game.operation_flags, GOF_ShowGui);

    if (show_sidebar)
    {
        draw_whole_status_panel();
    }

    if (!menu_open)
    {
        draw_gui();
        if (show_sidebar)
        {
            draw_overlay_compass(player->minimap_pos_x, player->minimap_pos_y);
        }
        message_draw();
        draw_tooltip();
    }

    draw_power_hand();
    if (should_render_ui())
    {
        gui_draw_all_boxes();
    }
    RendMenu_Draw();
}

/******************************************************************************/
// C shim

extern "C" {

void GameUI_DrawFrame(struct PlayerInfo* player)
{
    GameUI::Get().DrawFrame(player);
}

} // extern "C"
