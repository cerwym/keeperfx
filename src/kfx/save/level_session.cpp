/******************************************************************************/
// KeeperFX — Level lifecycle management.
/******************************************************************************/
#include "kfx/save/level_session.h"

#include "globals.h"
#include "game_legacy.h"           /* game macro (*gpGame), get_loaded_level_number, get_level_fgroup */
#include "keeperfx.hpp"            /* init_lookups, reinit_tagged_blocks_for_player, engine_palette, start_params */
#include "ariadne.h"               /* init_navigation */
#include "engine_redraw.h"         /* set_engine_view */
#include "engine_textures.h"       /* load_texture_map_file, init_animating_texture_maps */
#include "frontend.h"              /* init_gui */
#include "frontmenu_ingame_map.h"  /* update_panel_colors, update_panel_color_player_color */
#include "frontmenu_ingame_tabs.h" /* update_room/powers/trap_tab_to_config */
#include "gui_parchment.h"         /* parchment_loaded */
#include "gui_topmsg.h"            /* erstats_clear */
#include "player_computer.h"       /* restore_computer_player_after_load */
#include "player_data.h"           /* get_player, get_my_player */
#include "room_list.h"             /* start_rooms, end_rooms */
#include "sounds.h"                /* sound_reinit_after_load */
#include "ui_init.h"               /* init_gameplay_ui */
#include "creature_states_combt.h" /* reset_postal_instance_cache */
#include "renderer/RendererManager.h" /* RendererNeedsUIReinitAfterLoad */

/******************************************************************************/

void LevelSession_ReinitGameState(void)
{
    struct PlayerInfo *player = get_my_player();
    player->lens_palette = 0;
    player->main_palette = engine_palette;
    init_lookups();
    init_navigation();
    reinit_packets_after_load();
    game.easter_eggs_enabled = start_params.easter_egg;
    parchment_loaded = 0;
    /* View modes and panel colours — must precede tab-config calls below. */
    int i;
    for (i = 0; i < PLAYERS_COUNT; i++) {
        player = get_player(i);
        if (player_exists(player)) {
            set_engine_view(player, player->view_mode);
            update_panel_color_player_color(player->id_number,
                                            get_dungeon(i)->color_idx);
        }
    }
    start_rooms = &game.rooms[1];
    end_rooms   = &game.rooms[ROOMS_COUNT];
    update_room_tab_to_config();
    update_powers_tab_to_config();
    update_trap_tab_to_config();
}

void LevelSession_ReinitMapData(void)
{
    load_texture_map_file(game.texture_id,
                          get_loaded_level_number(),
                          get_level_fgroup(get_loaded_level_number()));
    init_animating_texture_maps();
}

void LevelSession_ReinitUI(void)
{
    if (RendererNeedsUIReinitAfterLoad()) {
        init_gui();
        init_gameplay_ui(UIPROLE_ACTIVE_PLAYER, game.active_players_count > 1);
    }
    /* Caller must invoke reset_gui_based_on_player_mode() to open the correct
       initial panels via the lazy turn_on_menu() path. */
}

void LevelSession_ReinitPlayerState(void)
{
    erstats_clear();
    struct PlayerInfo *player = get_my_player();
    reinit_tagged_blocks_for_player(player->id_number);
    reset_postal_instance_cache();
}

void LevelSession_ReinitAI(void)
{
    restore_computer_player_after_load();
}

void LevelSession_ReinitAudio(void)
{
    sound_reinit_after_load();
    update_panel_colors();
}

void LevelSession_ReinitAfterLoad(void)
{
    LevelSession_ReinitGameState();
    LevelSession_ReinitMapData();
    LevelSession_ReinitUI();
    reset_gui_based_on_player_mode();
    LevelSession_ReinitPlayerState();
    LevelSession_ReinitAI();
    LevelSession_ReinitAudio();
}

