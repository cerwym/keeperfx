/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_redraw.c
 *     Functions to redraw the engine screen.
 * @par Purpose:
 *     High level redrawing routines.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     06 Nov 2010 - 03 Jul 2011
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "engine_redraw.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_sprfnt.h"
#include "bflib_sound.h"
#include "bflib_mouse.h"
#include "bflib_dernc.h"
#include "lvl_script.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "player_instances.h"
#include "config_players.h"
#include "kjm_input.h"
#include "gui_parchment.h"
#include "gui_draw.h"
#include "gui_boxmenu.h"
#include "gui_msgs.h"
#include "gui_tooltips.h"
#include "power_hand.h"
#include "power_process.h"
#include "engine_render.h"
#include "engine_lenses.h"
#include "local_camera.h"
#include "front_simple.h"
#include "front_easter.h"
#include "frontend.h"
#include "ui_init.h"
#include "frontmenu_ingame_tabs.h"
#include "kfx/engine/cameras.h"
#include "kfx/imgui/DevTools.h"
#include "kfx/ui/GameUI.h"
#include "frontmenu_ingame_evnt.h"
#include "frontmenu_ingame_map.h"
#include "creature_graphics.h"
#include "vidmode.h"
#include "mouse_cursor.h"
#include "config.h"
#include "config_strings.h"
#include "config_terrain.h"
#include "config_magic.h"
#include "config_spritecolors.h"
#include "magic_powers.h"
#include "game_merge.h"
#include "game_legacy.h"
#include "creature_instances.h"
#include "packets.h"
#include "custom_sprites.h"
#include "keeperfx.hpp"
#include "renderer/RendererManager.h"
#include "renderer/RendMenuOverlay.h"
#include "kfx/profiling/KfxProfilingC.h"
#include "gui/gui_bridge.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
int32_t xtab[640][2];
int32_t ytab[480][2];

static long draw_spell_cost;
/******************************************************************************/
static void draw_creature_view_icons(struct Thing* creatng)
{
    struct GuiMenu *gmnu = get_active_menu(menu_id_to_number(GMnu_MAIN));
    ScreenCoord x = gmnu->width + scale_value_by_horizontal_resolution(5);
    ScreenCoord y;
    const struct TbSprite* spr;
    int ps_units_per_px;
    {
        spr = get_panel_sprite(488);
        ps_units_per_px = (22 * units_per_pixel) / spr->SHeight;
        y = RendererGetScreenHeight() - scale_ui_value_lofi(spr->SHeight * 2);
    }
    struct CreatureControl *cctrl = creature_control_get_from_thing(creatng);
    for (SpellKind spell_idx = 0; spell_idx < CREATURE_MAX_SPELLS_CASTED_AT; spell_idx++)
    {
        struct CastedSpellData* cspell = &cctrl->casted_spells[spell_idx];
        if (cspell->spkind == 0)
        {
            continue;
        }
        struct SpellConfig *spconf = get_spell_config(cspell->spkind);
        long spridx = spconf->medsym_sprite_idx;
        if (flag_is_set(spconf->spell_flags, CSAfF_Invisibility))
        {
            if (cctrl->force_visible & 2)
            {
                spridx++;
            }
        }
        if (flag_is_set(spconf->spell_flags, CSAfF_Timebomb))
        {
            int tx_units_per_px = (dbc_language > 0) ? scale_ui_value_lofi(16) : (22 * units_per_pixel) / LbTextLineHeight();
            int h = LbTextLineHeight() * tx_units_per_px / 16;
            int w = scale_ui_value_lofi(spr->SWidth);
            if (dbc_language > 0)
            {
                if (RendererGetScreenHeight() < 400)
                {
                    w *= 2;
                }
            }
            LbTextSetWindow(x + scale_ui_value_lofi(spr->SWidth / 2), y - scale_ui_value_lofi(spr->SHeight), w, h);
            TextRenderer_SetDrawColour(LbTextGetFontFaceColor(winfont));
            TextRenderer_SetShadowColour(LbTextGetFontBackColor(winfont));
            char text[16];
            snprintf(text, sizeof(text), "%u", (cctrl->timebomb_countdown / turns_per_second));
            LbTextDrawResized(0, 0, tx_units_per_px, text, Lb_TEXT_HALIGN_CENTER);
        }
        draw_gui_panel_sprite_left(x, y, ps_units_per_px, spridx, 0);
        x += scale_ui_value_lofi(spr->SWidth);
    }
    if ( (cctrl->dragtng_idx != 0) && ((creatng->alloc_flags & TAlF_IsDragged) == 0) )
    {
        struct Thing* dragtng = thing_get(cctrl->dragtng_idx);
        unsigned long spr_idx;
        x = RendererGetScreenWidth() - (scale_value_by_horizontal_resolution(148) / 4);
        switch(dragtng->class_id)
        {
            case TCls_Object:
            {
                RoomKind rkind;
                struct RoomConfigStats *roomst;
                if (thing_is_workshop_crate(dragtng))
                {
                    rkind = find_first_roomkind_with_role(RoRoF_CratesStorage);
                }
                else
                {
                    rkind = find_first_roomkind_with_role(RoRoF_PowersStorage);
                }
                roomst = get_room_kind_stats(rkind);
                spr_idx = roomst->medsym_sprite_idx;
                break;
            }
            case TCls_DeadCreature:
            case TCls_Creature:
            {
                y -= scale_value_by_horizontal_resolution(spr->SHeight / 2);
                spr_idx = get_creature_model_graphics(dragtng->model, CGI_HandSymbol);
                if (dragtng->class_id == TCls_DeadCreature)
                {
                    spr_idx++;
                }
                break;
            }
            default:
            {
                spr_idx = 0;
                break;
            }
        }
        draw_gui_panel_sprite_left(x, y, ps_units_per_px, spr_idx, 0);
    }
    else
    {
        struct PlayerInfo* player = get_my_player();
        if (player->view_type == PVT_CreatureContrl)
        {
            if (!creature_instance_is_available(creatng, cctrl->active_instance_id))
            {
                x = RendererGetScreenWidth() - (scale_value_by_horizontal_resolution(148) / 4);
                struct InstanceInfo* inst_inf = creature_instance_info_get(cctrl->active_instance_id % game.conf.crtr_conf.instances_count);
                draw_gui_panel_sprite_left(x, y, ps_units_per_px, inst_inf->symbol_spridx, 0);
            }
        }
    }
}

void setup_engine_window(long x, long y, long width, long height)
{
    SYNCDBG(6,"Starting for size (%ld,%ld) at (%ld,%ld)",width,height,x,y);
    struct PlayerInfo* player = get_my_player();
    if ((game.operation_flags & GOF_ShowGui) != 0)
    {
      if (x > RendererGetScreenWidth())
        x = RendererGetScreenWidth();
      // GPU renderers draw the world fullscreen and paint the sidebar on top,
      // so the engine window is NOT clamped to status_panel_width.
      if (!RendererWantsFullscreenViewport())
      {
        if (x < status_panel_width)
          x = status_panel_width;
      }
      else
      {
        if (x < 0)
          x = 0;
      }
    } else
    {
      if (x > RendererGetScreenWidth())
        x = RendererGetScreenWidth();
      if (x < 0)
        x = 0;
    }
    if (y > RendererGetScreenHeight())
      y = RendererGetScreenHeight();
    if (y < 0)
      y = 0;
    if (x+width > RendererGetScreenWidth())
      width = RendererGetScreenWidth()-x;
    if (width < 0)
      width = 0;
    if (y+height > RendererGetScreenHeight())
      height = RendererGetScreenHeight()-y;
    if (height < 0)
      height = 0;
    player->engine_window_x = x;
    player->engine_window_y = y;
    player->engine_window_width = width;
    player->engine_window_height = height;
}

void store_engine_window(TbGraphicsWindow *ewnd,int divider)
{
    struct PlayerInfo* player = get_my_player();
    if (divider <= 1)
    {
        ewnd->x = player->engine_window_x;
        ewnd->y = player->engine_window_y;
        ewnd->width = player->engine_window_width;
        ewnd->height = player->engine_window_height;
    } else
    {
        ewnd->x = player->engine_window_x/divider;
        ewnd->y = player->engine_window_y/divider;
        ewnd->width = player->engine_window_width/divider;
        ewnd->height = player->engine_window_height/divider;
    }
    ewnd->ptr = NULL;
}

// RENDER-SW-IMPL: software implementation of IMapFadePass — per-pixel wipe into the software draw surface (outbuf).
// Hardcoded to 320×200; hardware path would use a UV-warp fragment shader at native resolution.
// Called directly by SoftwareMapFadePass::StepFadeIn/StepFadeOut via map_fade_in/map_fade_out.
void map_fade(unsigned char *outbuf, unsigned char *srcbuf1, unsigned char *srcbuf2, unsigned char *fade_tbl, unsigned char *ghost_tbl, long a6, long const xmax, long const ymax, long a9)
{
    long ix;
    long iy;
    long x1base = 4 * a6;
    long x0base = 4 * (32 - a6);
    int32_t * xt = xtab[0];
    int vx0 = 0;
    int vx1 = 0;
    for (ix = xmax; ix > 0; ix--)
    {
        long val = x1base + vx1 / xmax;
        long m;
        if (val >= 0)
        {
            m = min(xmax,val);
        }
        else
        {
            m = 0;
        }
        xt[1] = m;
        val = x0base + vx0 / xmax;
        if (val >= 0) {
            m = min(xmax,val);
        } else {
            m = 0;
        }
        xt[0] = m;
        xt += 2;
        vx0 += xmax - 8 * (32 - a6);
        vx1 += xmax - 8 * a6;
    }

    long y1base = 8 * ymax / xmax * x1base / 8;
    long y0base = 8 * ymax / xmax * x0base / 8;
    int32_t * yt = ytab[0];
    int vy1 = 0;
    int vy0 = 0;
    for (iy = ymax; iy > 0; iy--)
    {
        long val = y1base + vy1 / ymax;
        long m;
        if (val >= 0)
        {
            m = min(ymax,val);
        }
        else
        {
            m = 0;
        }
        yt[1] = xmax * m;
        val = y0base + vy0 / ymax;
        if (val >= 0)
        {
            m = min(ymax,val);
        } else {
            m = 0;
        }
        yt[0] = xmax * m;
        yt += 2;
        vy0 += ymax - 2 * y0base;
        vy1 += ymax - 2 * y1base;
    }

    x0base = a6 << 8;
    y0base = (32 - a6) << 8;
    unsigned char* out = outbuf;
    yt = ytab[0];
    for (iy = ymax; iy > 0; iy--)
    {
        unsigned char* sbuf2 = &srcbuf2[yt[1]];
        unsigned char* sbuf1 = &srcbuf1[yt[0]];
        xt = xtab[0];
        for (ix = xmax; ix > 0; ix--)
        {
            int px1 = fade_tbl[x0base + sbuf1[xt[0]]];
            int px2 = fade_tbl[y0base + sbuf2[xt[1]]];
            *out = ghost_tbl[256 * px2 + px1];
            out++;
            xt += 2;
        }
        out += a9 - xmax;
        yt += 2;
    }
}

void generate_map_fade_ghost_table(const char *fname, unsigned char *palette, unsigned char *ghost_table)
{
    if (LbFileLoadAt(fname, ghost_table) != PALETTE_COLORS*PALETTE_COLORS)
    {
        unsigned char* out = ghost_table;
        for (int i = 0; i < PALETTE_COLORS; i++)
        {
            unsigned char* bpal = &palette[3 * i];
            for (int n = 0; n < PALETTE_COLORS; n++)
            {
                unsigned char* spal = &palette[3 * n];
                unsigned char r = bpal[0] + spal[0];
                unsigned char g = bpal[1] + spal[1];
                unsigned char b = bpal[2] + spal[2];
                *out = LbPaletteFindColour(palette, r, g, b);
                out++;
            }
        }
        LbFileSaveAt(fname, ghost_table, PALETTE_COLORS*PALETTE_COLORS);
    }
}

int get_place_room_pointer_graphics(RoomKind rkind) {
    struct RoomConfigStats* roomst = get_room_kind_stats(rkind);
    return roomst->pointer_sprite_idx;
}

int get_place_trap_pointer_graphics(ThingModel trmodel) {
    struct TrapConfigStats* trapst = get_trap_model_stats(trmodel);
    return trapst->pointer_sprite_idx;
}

int get_place_door_pointer_graphics(ThingModel drmodel) {
    struct DoorConfigStats* doorst = get_door_model_stats(drmodel);
    return doorst->pointer_sprite_idx;
}

/**
 * Renders source and destination screens for map fading.
 * Stores them in given buffers.
 * @param fade_src
 * @param fade_dest
 * @param scanline Line width of the two given buffers.
 * @param height Height to be filled in given buffers.
 */
// RENDER-SW-IMPL: captures two rendered frames into CPU buffers as source/dest for map_fade().
// Hardware path would replace with render-to-texture; both captures would be GPU framebuffers.
void prepare_map_fade_buffers(unsigned char *fade_src, unsigned char *fade_dest, int scanline, int height)
{
    MapFadePass_PrepareBuffers(fade_src, fade_dest, scanline, height);
}
 
long map_fade_in(long palette_fade_step)
{
    SYNCDBG(6,"Starting");
    return MapFadePass_StepFadeIn(palette_fade_step);
}
 
long map_fade_out(long palette_fade_step)
{
    SYNCDBG(6,"Starting");
    return MapFadePass_StepFadeOut(palette_fade_step);
}

long dummy_sound_line_of_sight(long a1, long a2, long a3, long a4, long a5, long a6)
{
    return 1;
}

void set_engine_view(struct PlayerInfo *player, long val)
{
    switch ( val )
    {
    case PVM_EmptyView:
        camera_set_active(player->id_number, CamIV_Isometric);
        // Allow view mode 0 only for non-local-human players
        if (!is_my_player(player))
            break;
        // If it's local human player, then setting this mode is an error
        // fall through
    default:
        ERRORLOG("Invalid view mode %d",(int)val);
        val = PVM_CreatureView;
        // fall through
    case PVM_CreatureView:
        camera_set_active(player->id_number, CamIV_FirstPerson);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 2;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(0);
        LbMouseSetPosition(RendererScreenWidth() >> 1, RendererScreenHeight() >> 1);
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        camera_set_active(player->id_number, CamIV_Isometric);
        camera_get_active(player->id_number)->view_mode = val;
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 0;
        // no need to set temp_cluedo_mode here; it's done in update_engine_settings
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    case PVM_ParchmentView:
        camera_set_active(player->id_number, CamIV_Parchment);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    case PVM_ParchFadeIn:
    case PVM_ParchFadeOut:
        // In fade states, keep the settings unchanged
        break;
    case PVM_FrontView:
        camera_set_active(player->id_number, CamIV_FrontView);
        sync_local_camera(player);
        if (!is_my_player(player))
            break;
        lens_mode = 0;
        temp_cluedo_mode = 0;
        S3DSetLineOfSightFunction(dummy_sound_line_of_sight);
        S3DSetDeadzoneRadius(1280);
        break;
    }
    player->view_mode = val;
}

void draw_overlay_compass(long base_x, long base_y)
{
    struct PlayerInfo* player = get_my_player();
    struct Camera* cam = get_local_active_camera(player->id_number);
    LbTextSetFont(winfont);
    TbDrawFlagsMask compass_flags = Lb_SPRITE_TRANSPAR4;
    LbTextSetWindow(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
    int units_per_px = (16 * status_panel_width + 140 / 2) / 140;
    int tx_units_per_px = (22 * units_per_px) / LbTextLineHeight();
    int w = (LbSprFontCharWidth(winfont, '/') * tx_units_per_px / 16) / 2;
    int h = (LbSprFontCharHeight(winfont, '/') * tx_units_per_px / 16) / 2 + 2 * units_per_px / 16;
    int center_x = base_x * units_per_px / 16 + MapDiagonalLength / 2;
    int center_y = base_y * units_per_px / 16 + MapDiagonalLength / 2;
    int shift_x = (-(MapDiagonalLength * 7 / 16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    int shift_y = (-(MapDiagonalLength * 7 / 16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    if (RendererIsFrameOpen()) {
        LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapN), compass_flags);
    }
    shift_x = ( (MapDiagonalLength*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = ( (MapDiagonalLength*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapS), compass_flags);
    shift_x = ( (MapDiagonalLength*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = (-(MapDiagonalLength*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapE), compass_flags);
    shift_x = (-(MapDiagonalLength*7/16) * LbCosL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    shift_y = ( (MapDiagonalLength*7/16) * LbSinL(cam->rotation_angle_x)) >> LbFPMath_TrigmBits;
    LbTextDrawResized(center_x + shift_x - w, center_y + shift_y - h, tx_units_per_px, get_string(GUIStr_MapW), compass_flags);
}

// Because of the way this is used for the dungeon heart fly-in effect, this remains un-refactored until I can
// understand the setup required for the fly-in and how to make it work with the new rendering system.
void redraw_creature_view(void)
{
    KFX_C_ZONE_BEGIN_COLOR(ctx, "Render/CreatureView", KFX_COLOR_RENDER_CPU);
    SYNCDBG(6, "Starting");
    struct PlayerInfo* player = get_my_player();
    update_explored_flags_for_power_sight(player);
    struct Thing* thing = thing_get(player->controlled_thing_idx);
    TRACE_THING(thing);
    if (thing_exists(thing))
        draw_creature_view(thing);
    remove_explored_flags_for_power_sight(player);
    if ((game.operation_flags & GOF_ShowGui) != 0) {
        draw_whole_status_panel();
    }
    TbBool menu_open = RendMenu_IsOpen();
    if (!menu_open) {
        draw_gui();
        if ((game.operation_flags & GOF_ShowGui) != 0) {
            draw_overlay_compass(player->minimap_pos_x, player->minimap_pos_y);
        }
        message_draw();
        draw_tooltip();
    }
    gui_draw_all_boxes();
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (!creature_control_invalid(cctrl)) {
        draw_creature_view_icons(thing);
        if (!gui_box_is_not_valid(gui_cheat_box_3)) {
            struct GuiBoxOption* guop = gui_cheat_box_3->optn_list;
            while (guop->label[0] != '!') {
                guop->active = (cctrl->active_instance_id == guop->cb_param1);
                guop++;
            }
        }
    }
    KFX_C_ZONE_END(ctx);
}

// The default angled view.
void redraw_isometric_view(void)
{
    KFX_C_ZONE_BEGIN_COLOR(ctx, "Render/IsometricView", KFX_COLOR_RENDER_CPU);
    struct PlayerInfo* player = get_my_player();
    if (!camera_is_active(player->id_number))
        return;
    TbGraphicsWindow ewnd;
    memset(&ewnd, 0, sizeof(TbGraphicsWindow));
    struct Camera* render_cam = get_local_camera(CamIV_Isometric);
    update_explored_flags_for_power_sight(player);
    engine(player,render_cam);
    remove_explored_flags_for_power_sight(player);
    draw_2d_elements(player);
    KFX_C_ZONE_END(ctx);
}

void redraw_frontview(void)
{
    KFX_C_ZONE_BEGIN_COLOR(ctx, "Render/FrontView", KFX_COLOR_RENDER_CPU);
    SYNCDBG(6,"Starting");
    struct PlayerInfo* player = get_my_player();
    struct Camera* render_cam = get_local_camera(CamIV_FrontView);
    update_explored_flags_for_power_sight(player);
    draw_frontview_engine(render_cam);
    remove_explored_flags_for_power_sight(player);
    draw_2d_elements(player);
    KFX_C_ZONE_END(ctx);
}

// Draws 2D elements on top of 3D view, like spell cursor. Called from redraw_isometric_view() and redraw_frontview()
// after 3D rendering is done.
// Delegates to GameUI (src/kfx/ui/GameUI.cpp), which owns the composed
// in-game UI as a single IR layer drawn after all world content -- see
// that file for why this is no longer split across Back/Front layers.
void draw_2d_elements(struct PlayerInfo* player) {
    GameUI_DrawFrame(player);
}

/**
 * Sets the mouse pointer to the cursor for the given power/spell.
 *
 * @param pwkind The power to show a cursor for; <= 0 hides the cursor.
 * @param stl_x,stl_y  Target subtile (for the availability check at this spot).
 * @return true if a spell cursor was set; false if the spell wasn't castable
 *         here and an invisible/deny cursor was set instead.
 *
 * Thing-specific cast conditions are skipped here (CastChk_SkipThing); callers
 * that care about the target thing validate it before calling.
 */
TbBool draw_spell_cursor(PowerKind pwkind, MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    long i;
    struct PlayerInfo* player = get_my_player();
    SYNCDBG(5,"Starting for power %d",(int)pwkind);
    if (pwkind <= 0)
    {
        set_pointer_graphic(MousePG_Invisible);
        return false;
    }

    const struct PowerConfigStats* powerst = get_power_model_stats(pwkind);
    TbBool allow_cast = can_cast_spell(player->id_number, pwkind, stl_x, stl_y, INVALID_THING, CastChk_SkipThing);
    if (!allow_cast)
    {
        set_pointer_graphic(MousePG_DenyMark);
        return false;
    }
    Expand_Check_Func chkfunc = powermodel_expand_check_func_list[powerst->overcharge_check_idx];
    if (chkfunc != NULL)
    {
        if (chkfunc())
        {
            i = get_power_overcharge_level(player);
            set_pointer_graphic(MousePG_SpellCharge0+i);
            draw_spell_cost = compute_power_price(player->id_number, pwkind, i);

            // cheat mode. Everything is free. show charging level instead of none when cost is zero.
            if (draw_spell_cost == 0)
                draw_spell_cost = -(i+1);

            return true;
        }
    }
    i = get_player_colored_pointer_icon_idx(powerst->pointer_sprite_idx,my_player_number);
    set_pointer_graphic_spell(i, get_gameturn());
    return true;
}

/** Pick the cursor when the CtrlDungeon power-hand tool is active: possession
 *  hover, creature query, or the plain power-hand/pickaxe/invisible fallback.
 *  Extracted from process_dungeon_top_pointer_graphic() for readability.
 *  Note: intentionally still mutates player->thing_under_hand and sets
 *  PlaF6_DisplayNeedsUpdate (consumed in power_hand.c) — behaviour preserved. */
static void set_ctrldungeon_powerhand_pointer(struct PlayerInfo *player, struct Dungeon *dungeon)
{
    short thing_under_hand = player->thing_under_hand;
    if (local_thing_under_hand > 0) {
        thing_under_hand = local_thing_under_hand;
    }
    struct Thing *thing = thing_get(thing_under_hand);
    TRACE_THING(thing);
    TbBool can_cast = false;
    // Possession-mode hover: input_crtr_control is set while the player is in
    // "control creature" input mode (possession key/button held). If the thing
    // under the hand is not the one already held, show the possession spell
    // cursor instead of the plain power hand.
    if ((player->input_crtr_control) && (thing_exists(thing)) && (dungeon->things_in_hand[0] != thing_under_hand))
    {
        PowerKind pwkind = PwrK_POSSESS;
        if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
        {
            // The condition above makes can_cast_spell() within draw_spell_cursor() to never fail; this is intentional
            can_cast = true;
        }
        else
        {
            thing = get_creature_near_for_controlling(player->id_number, thing->mappos.x.val, thing->mappos.y.val);
            if (!thing_is_invalid(thing))
            {
                if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
                {
                    can_cast = true;
                }
            }
        }
        // Show the possession pointer for the hovered creature. draw_spell_cursor
        // only sets the mouse graphic (no cast, no world geometry); pwkind is
        // passed explicitly so player->chosen_power_kind stays untouched (it is
        // PwrK_None here — work_state is still CtrlDungeon).
        if (can_cast)
        {
            draw_spell_cursor(pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num);
            player->thing_under_hand = thing->index;
        } else {
            set_pointer_graphic(MousePG_Arrow);
        }

        player->display_flags |= PlaF6_DisplayNeedsUpdate;
    } else
    if (((player->input_crtr_query) && !thing_is_invalid(thing)) && (dungeon->things_in_hand[0] != thing_under_hand)
        && can_thing_be_queried(thing, player->id_number))
    {
        set_pointer_graphic(MousePG_Query);
        player->display_flags |= PlaF6_DisplayNeedsUpdate;
    } else
    {
        if ((player->additional_flags & PlaAF_ChosenSubTileIsHigh) != 0) {
          set_pointer_graphic((player->roomspace_highlight_mode == drag_placement_mode) ? MousePG_Pickaxe2 : MousePG_Pickaxe);
        } else {
          set_pointer_graphic(MousePG_Invisible);
        }
    }
}

void process_dungeon_top_pointer_graphic(struct PlayerInfo *player)
{
    struct Thing *thing;
    struct Dungeon* dungeon = get_dungeon(player->id_number);
    struct PlayerStateConfigStats* plrst_cfg_stat = get_player_state_stats(player->work_state);
    if (dungeon_invalid(dungeon))
    {
        set_pointer_graphic(MousePG_Invisible);
        return;
    }
    // During fade
    if (player->instance_num == PI_MapFadeFrom)
    {
        set_pointer_graphic(MousePG_Invisible);
        return;
    }
    // Mouse over panel map
    if (((game.operation_flags & GOF_ShowGui) != 0) && mouse_is_over_panel_map(player->minimap_pos_x, player->minimap_pos_y))
    {
        if (game.small_map_state == 2) {
            set_pointer_graphic(MousePG_Invisible);
        } else {
            set_pointer_graphic(MousePG_Arrow);
        }
        return;
    }
    // Mouse over battle message box
    if (battle_creature_over > 0)
    {
        PowerKind pwkind = player->chosen_power_kind;
        thing = thing_get(battle_creature_over);
        TRACE_THING(thing);
        if (can_cast_spell(player->id_number, pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num, thing, CastChk_Default))
        {
            draw_spell_cursor(pwkind, thing->mappos.x.stl.num, thing->mappos.y.stl.num);
        } else
        {
            set_pointer_graphic(MousePG_Arrow);
        }
        return;
    }
    // GUI action being processed
    if (game_is_busy_doing_gui())
    {
        set_pointer_graphic(MousePG_Arrow);
        return;
    }
    long i;
    switch (plrst_cfg_stat->pointer_group)
    {
    case PsPg_CtrlDungeon:
        if (player->secondary_cursor_state)
          i = player->secondary_cursor_state;
        else
          i = player->primary_cursor_state;
        if ((player->instance_num == PI_Grab) || (player->instance_num == PI_Drop) || (player->instance_num == PI_Whip) || (player->instance_num == PI_WhipEnd) || (local_thing_under_hand > 0)) {
            i = CSt_PowerHand;
        } else
        if ((i == CSt_PowerHand) && power_hand_is_empty(player))
        {
            i = CSt_DefaultArrow;
        }
        switch (i)
        {
        case CSt_PickAxe:
        {
            set_pointer_graphic((player->roomspace_highlight_mode == drag_placement_mode) ? MousePG_Pickaxe2 : MousePG_Pickaxe);
            break;
        }
        case CSt_DoorKey:
            set_pointer_graphic(MousePG_LockMark);
            break;
        case CSt_PowerHand:
            set_ctrldungeon_powerhand_pointer(player, dungeon);
            break;
        default:
            if (player->hand_busy_until_turn <= get_gameturn())
              set_pointer_graphic(MousePG_Arrow);
            else
              set_pointer_graphic(MousePG_Invisible);
            break;
        }
        break;
    case PsPg_BuildRoom:
        i = get_place_room_pointer_graphics(player->chosen_room_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_Invisible:
        set_pointer_graphic(MousePG_Invisible);
        break;
    case PsPg_Spell:
        draw_spell_cursor(player->chosen_power_kind, game.mouse_light_pos.x.stl.num, game.mouse_light_pos.y.stl.num);
        break;
    case PsPg_Query:
        set_pointer_graphic(MousePG_Query);
        break;
    case PsPg_PlaceTrap:
        i = get_place_trap_pointer_graphics(player->chosen_trap_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_PlaceDoor:
        i = get_place_door_pointer_graphics(player->chosen_door_kind);
        set_pointer_graphic(i);
        break;
    case PsPg_Sell:
        set_pointer_graphic(MousePG_Sell);
        break;
    case PsPg_PlaceTerrain:
    {
        i = get_place_terrain_pointer_graphics(player->cheatselection.chosen_terrain_kind);
        set_pointer_graphic(i);
        break;
    }
    case PsPg_MkDigger:
        set_pointer_graphic(MousePG_MkDigger);
        break;
    case PsPg_MkCreatr:
        set_pointer_graphic(MousePG_MkCreature);
        break;
    case PsPg_OrderCreatr:
    {
        struct Thing* creatng = thing_get(player->controlled_thing_idx);
        i = (thing_is_creature(creatng)) ? MousePG_MvCreature : MousePG_Arrow;
        set_pointer_graphic(i);
        break;
    }
    case PsPg_None:
    default:
        set_pointer_graphic(MousePG_Arrow);
        break;
    }
}

void process_pointer_graphic(void)
{
    struct PlayerInfo* player = get_my_player();
    SYNCDBG(6,"Starting for view %d, player state %s, instance %d",(int)player->view_type,player_state_code_name(player->work_state),(int)player->instance_num);
    if (KfxDevTools_WantCaptureMouse())
    {
        set_pointer_graphic(MousePG_Invisible);
        return;
    }
    switch (player->view_type)
    {
    case PVT_DungeonTop:
        // This case is complicated
        process_dungeon_top_pointer_graphic(player);
        break;
    case PVT_CreatureContrl:
    case PVT_CreaturePasngr:
        if (a_menu_window_is_active())
          set_pointer_graphic(MousePG_Arrow);
        else
          set_pointer_graphic(MousePG_Invisible);
        break;
    case PVT_MapScreen:
    case PVT_MapFadeIn:
    case PVT_MapFadeOut:
        set_pointer_graphic(MousePG_Arrow);
        break;
    case PVT_None:
        set_pointer_graphic_none();
        break;
    default:
        WARNLOG("Unsupported view type");
        set_pointer_graphic_none();
        break;
    }
}

void redraw_display(void)
{
    SYNCDBG(5,"Starting");
    // Service the debug creature-sprite cache on the game thread (safe file I/O
    // + read-only config access). Cheap no-op unless the viewer requested a load.
    KfxDevTools_ServiceGameThread();
    struct PlayerInfo* player = get_my_player();
    player->display_flags &= ~PlaF6_DisplayNeedsUpdate;
    if (game.game_kind == GKind_NonInteractiveState)
      return;
    if (game.small_map_state == 2)
      set_pointer_graphic_none();
    else
      process_pointer_graphic();
    interpolate_local_cameras();
    switch (player->view_mode)
    {
    case PVM_EmptyView:
        break;
    case PVM_CreatureView:
        redraw_creature_view();
        GUIBridge_LeaveParchmentView();
        parchment_loaded = 0;
        break;
    case PVM_IsoWibbleView:
    case PVM_IsoStraightView:
        redraw_isometric_view();
        GUIBridge_LeaveParchmentView();
        parchment_loaded = 0;
        break;
    case PVM_ParchmentView:
        GUIBridge_DrawParchmentView();
        break;
    case PVM_FrontView:
        redraw_frontview();
        GUIBridge_LeaveParchmentView();
        parchment_loaded = 0;
        break;
    case PVM_ParchFadeIn:
        parchment_loaded = 0;
        player->palette_fade_step_map = MapFadePass_StepFadeIn(player->palette_fade_step_map);
        break;
    case PVM_ParchFadeOut:
        parchment_loaded = 0;
        player->palette_fade_step_map = MapFadePass_StepFadeOut(player->palette_fade_step_map);
        break;
    default:
        ERRORLOG("Unsupported drawing state, %d",(int)player->view_mode);
        break;
    }
    //LbTextSetWindow(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
    LbTextSetFont(winfont);
    int tx_units_per_px = ( (RendererGetScreenHeight() < 400) && (dbc_language > 0) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
    LbTextSetWindow(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
    if ((player->allocflags & PlaF_NewMPMessage) != 0)
    {
        char text[sizeof(player->mp_message_text) + 4];
        snprintf(text, sizeof(text), ">%s_", player->mp_message_text);
        long pos_x = 148*units_per_pixel/16;
        long pos_y = 8*units_per_pixel/16;
        if (game.armageddon_cast_turn != 0)
        {
            if ( (bonus_timer_enabled()) || (script_timer_enabled()) || display_variable_enabled() )
            {
                pos_y = ((pos_y << 3) + ((LbTextLineHeight()*units_per_pixel/16) * (game.active_messages_count << (RendererGetScreenHeight() < 400))));
            }
        }
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text, 0);
    }
    if ( draw_spell_cost )
    {
        LbTextSetWindow(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
        LbTextSetFont(winfont);
        char text[32];
        if (draw_spell_cost > 0)
            snprintf(text, sizeof(text), "%ld", draw_spell_cost);
	else
            snprintf(text, sizeof(text), "lv%ld", (-draw_spell_cost));
        long pos_y = GetMouseY() - (LbTextStringHeight(text) * units_per_pixel / 16) / 2 - 2 * units_per_pixel / 16;
        long pos_x = GetMouseX() - (LbTextStringWidth(text) * units_per_pixel / 16) / 2;
        LbTextDrawResized(pos_x, pos_y, tx_units_per_px, text, 0);
        draw_spell_cost = 0;
    }
    if (bonus_timer_enabled())
    {
        draw_bonus_timer();
    }
    else if (script_timer_enabled())
    {
        draw_script_timer(game.script_timer_player, game.script_timer_id, game.script_timer_limit, game.timer_real);
    }
    if (gameturn_timer_enabled())
    {
        draw_gameturn_timer();
    }
    if (display_variable_enabled())
    {
        draw_script_variable(game.script_variable_player, game.script_value_type, game.script_value_id, game.script_variable_target, game.script_variable_target_type);
    }
    if (timer_enabled())
    {
        draw_timer();
    }
    if (frametime_enabled())
    {
        draw_frametime();
    }
    if (debug_display_network_stats != 0)
    {
        draw_network_stats();
    }
    if (consolelog_enabled())
    {
        draw_consolelog();
    }

    if (((game.operation_flags & GOF_Paused) != 0) && ((game.operation_flags & GOF_WorldInfluence) == 0) && !unpausing_in_progress && !RendMenu_IsOpen())
    {
          LbTextSetFont(winfont);
          const char * text = get_string(GUIStr_PausedMsg);
          long w = (LbTextStringWidth(text) * units_per_pixel / 16 + 2 * (LbTextCharWidth(' ') * units_per_pixel / 16));
          long pos_x;
          if (
              player->view_mode == PVM_IsoWibbleView ||
              player->view_mode == PVM_FrontView ||
              player->view_mode == PVM_IsoStraightView ||
              player->view_mode == PVM_CreatureView
          ) {
              pos_x = player->engine_window_x + (RendererGetScreenWidth() - w - player->engine_window_x) / 2;
          } else {
              pos_x = (RendererGetScreenWidth()-w)/2;
          }
          long pos_y = 16 * units_per_pixel / 16;
          long h = LbTextLineHeight() * units_per_pixel / 16;
          int text_w = w;
          int text_x = pos_x;
          if (RendererGetScreenHeight() < 400)
          {
              w *= 2;
              h *= 3;
              text_w = w;
              if (dbc_language > 0)
              {
                  text_w += 32;
                  text_x -= 12;
              }
          }
          UIRenderer_BeginTopOverlay();
          LbTextSetWindow(text_x, pos_y, text_w, h);
          draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
          LbTextDrawResized(0, 0, tx_units_per_px, text, Lb_TEXT_HALIGN_CENTER);
          UIRenderer_EndTopOverlay();
          LbTextSetWindow(0, 0, RendererScreenWidth(), RendererScreenHeight());
    }
    if (game.armageddon_cast_turn != 0)
    {
        int i = 0;
        if (game.armageddon_cast_turn + game.conf.rules[game.armageddon_caster_idx].magic.armageddon_count_down <= get_gameturn())
        {
            if (game.armageddon_over_turn - game.conf.rules[game.armageddon_caster_idx].magic.armageddon_duration <= get_gameturn())
                i = game.armageddon_over_turn - get_gameturn();
        } else
        {
            i = get_gameturn() - game.armageddon_cast_turn - game.conf.rules[game.armageddon_caster_idx].magic.armageddon_count_down;
        }
        LbTextSetFont(winfont);
        char text[64];
        snprintf(text, sizeof(text), " %s %03d", get_string(get_power_name_strindex(PwrK_ARMAGEDDON)), i/2); // Armageddon message
        i = LbTextCharWidth(' ')*units_per_pixel/16;
        long w = LbTextStringWidth(text) * units_per_pixel / 16 + 6 * i;
        i = LbTextLineHeight()*units_per_pixel/16;
        long h = pixel_size * i + pixel_size * i / 2;
        if (RendererGetScreenHeight() < 400)
        {
            w *= 2;
            h *= 2;
        }
        long pos_x = RendererGetScreenWidth() - w - 16 * units_per_pixel / 16;
        long pos_y = 16 * units_per_pixel / 16;
        UIRenderer_BeginTopOverlay();
        LbTextSetWindow(pos_x, pos_y, w, h);
        draw_slab64k(pos_x, pos_y, units_per_pixel, w, h);
        LbTextDrawResized(0, 0, tx_units_per_px, text, Lb_TEXT_HALIGN_CENTER);
        UIRenderer_EndTopOverlay();
        LbTextSetWindow(0, 0, RendererScreenWidth(), RendererScreenHeight());
    }
    draw_eastegg();
  //show_onscreen_msg(8, "Physical(%d,%d) Graphics(%d,%d) Lens(%d,%d)", (int)RendererPhysicalWidth(), (int)RendererPhysicalHeight(), (int)RendererScreenWidth(), (int)RendererScreenHeight(), (int)eye_lens_width, (int)eye_lens_height);
    SYNCDBG(7,"Finished");
}

/**
 * Redraws the game display buffer.
 */
TbBool keeper_screen_redraw(void)
{
    SYNCDBG(5,"Starting");
    render_frame_number++;
    struct PlayerInfo* player = get_my_player();
    if (lens_mode != 0) {
        RendererClearScreen(144); // Very dark green
    } else {
        RendererClearScreen(0);
    }
    if (RendererBeginFrame())
    {
        setup_engine_window(player->engine_window_x, player->engine_window_y,
            player->engine_window_width, player->engine_window_height);
        redraw_display();
        return true;
    }
    return false;
}

int get_place_terrain_pointer_graphics(SlabKind skind)
{
    int result;
    switch (skind)
    {
        case SlbT_ROCK:
        {
            result = MousePG_PlaceImpRock;
            break;
        }
        case SlbT_GOLD:
        {
            result = MousePG_PlaceGold;
            break;
        }
        case SlbT_EARTH:
        case SlbT_TORCHDIRT:
        {
            result = MousePG_PlaceEarth;
            break;
        }
        case SlbT_WALLDRAPE:
        case SlbT_WALLTORCH:
        case SlbT_WALLWTWINS:
        case SlbT_WALLWWOMAN:
        case SlbT_WALLPAIRSHR:
        case SlbT_DAMAGEDWALL:
        {
            result = MousePG_PlaceWall;
            break;
        }
        case SlbT_PATH:
        {
            result = MousePG_PlacePath;
            break;
        }
        case SlbT_CLAIMED:
        {
            result = MousePG_PlaceClaimed;
            break;
        }
        case SlbT_LAVA:
        {
            result = MousePG_PlaceLava;
            break;
        }
        case SlbT_WATER:
        {
            result = MousePG_PlaceWater;
            break;
        }
        case SlbT_GEMS:
        {
            result = MousePG_PlaceGems;
            break;
        }
        default:
        {
            result = MousePG_Arrow;
            break;
        }
    }
    return result;
}

/******************************************************************************/
