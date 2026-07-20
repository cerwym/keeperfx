/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file gui_parchment.c
 *     The map parchment screen support functions.
 * @par Purpose:
 *     Functions to display and maintain Parchment view (map view) during
 *     the gameplay.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     23 May 2010 - 10 Jun 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "gui_parchment.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_vidraw.h"
#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_dernc.h"
#include "bflib_planar.h"
#include "vidfade.h"        // pixmap ghost tables
#include "renderer/RendererManager.h"
#include "custom_sprites.h"
#include "frontend.h"
#include "front_simple.h"
#include "config.h"
#include "gui_boxmenu.h"
#include "gui_tooltips.h"
#include "gui_draw.h"
#include "kjm_input.h"
#include "engine_render.h"
#include "engine_camera.h"
#include "map_data.h"
#include "map_blocks.h"
#include "player_data.h"
#include "config_strings.h"
#include "config_campaigns.h"
#include "config_creature.h"
#include "config_terrain.h"
#include "config_spritecolors.h"
#include "thing_data.h"
#include "thing_objects.h"
#include "thing_traps.h"
#include "creature_graphics.h"
#include "creature_states.h"
#include "creature_states_hero.h"
#include "power_hand.h"
#include "game_legacy.h"
#include "room_list.h"
#include "room_workshop.h"
#include "frontmenu_ingame_tabs.h"
#include "vidfade.h"
#include "sprites.h"
#include "player_instances.h"

#include "renderer/RendererManager.h"
#include "keeperfx.hpp"
#include "post_inc.h"

/******************************************************************************/
unsigned short engine_remap_texture_blocks(long stl_x, long stl_y, unsigned short tex_id);
/******************************************************************************/
int parchment_loaded;
unsigned char *hires_parchment;
/* Low-res parchment image (320x200); allocated on first use.  The hires
 * variant is allocated by the gui_load_files_640 resource table. */
static unsigned char *lores_parchment = NULL;
/******************************************************************************/
void load_parchment_file(void)
{
    if ( !parchment_loaded )
    {
      reload_parchment_file(RendererPhysicalWidth() >= 640);
    }
}

void reload_parchment_file(TbBool hires)
{
  char *fname;
  if (hires)
  {
      fname = prepare_file_path(FGrp_StdData,"gmap64.raw");
      LbFileLoadAt(fname, hires_parchment);
  } else
  {
      fname = prepare_file_path(FGrp_StdData,"gmap32.raw");
      if (lores_parchment == NULL)
          lores_parchment = (unsigned char *)KfxAlloc(320*200);
      LbFileLoadAt(fname, lores_parchment);
  }
  parchment_loaded = 1;
}

long get_parchment_background_area_rect(struct TbRect *bkgnd_area)
{
    int img_width;
    int img_height;
    if (RendererPhysicalWidth() < 640)
    {
        img_width = 320;
        img_height = 200;
    } else
    {
        img_width = 640;
        img_height = 480;
    }
    int rect_w = RendererPhysicalWidth();
    int rect_h = RendererPhysicalHeight();
    // Parchment bitmap scaling
    int units_per_px = max(16 * rect_w / img_width, 16 * rect_h / img_height);
    int units_per_px_max = min(16 * 7 * rect_w / (6 * img_width), 16 * 4 * rect_h / (3 * img_height));
    if (units_per_px > units_per_px_max)
        units_per_px = units_per_px_max;
    // The image width can't be larger than video resolution
    if (units_per_px < 1) {
        units_per_px = 1;
    }
    // Set rectangle coords
    bkgnd_area->left = (rect_w-units_per_px*img_width/16)/2;
    bkgnd_area->top = (rect_h-units_per_px*img_height/16)/2;
    if (bkgnd_area->top < 0) bkgnd_area->top = 0;
    bkgnd_area->right = bkgnd_area->left + units_per_px*img_width/16;
    bkgnd_area->bottom = bkgnd_area->top + units_per_px*img_height/16;
    if (bkgnd_area->bottom > rect_h) bkgnd_area->bottom = rect_h;
    return units_per_px;
}

long get_parchment_map_area_rect(struct TbRect *map_area)
{
    struct TbRect bkgnd_area;
    get_parchment_background_area_rect(&bkgnd_area);
    long bkgnd_width = bkgnd_area.right - bkgnd_area.left;
    long bkgnd_height = bkgnd_area.bottom - bkgnd_area.top;
    long block_size = min((bkgnd_width - bkgnd_width / 3) / game.map_tiles_x, (bkgnd_height - bkgnd_height / 8) / game.map_tiles_y);
    if (block_size < 1) block_size = 1;
    map_area->left = bkgnd_area.left + (bkgnd_width - block_size*game.map_tiles_x) / 2;
    map_area->top = bkgnd_area.top + 3 * (bkgnd_height - block_size*game.map_tiles_y) / 4;
    map_area->right = map_area->left + block_size*game.map_tiles_x;
    map_area->bottom = map_area->top + block_size*game.map_tiles_y;
    return block_size;
}

TbBool point_to_overhead_map(const struct Camera *camera, const long screen_x, const long screen_y, int32_t *map_x, int32_t *map_y)
{
    // Sizes of the parchment map on which we are
    struct TbRect map_area;
    long block_size = get_parchment_map_area_rect(&map_area);
    // Check if we're within coordinates with the screen position
    *map_x = 0;
    *map_y = 0;
    if ((screen_x >= map_area.left) && (screen_x < map_area.right)
      && (screen_y >= map_area.top) && (screen_y < map_area.bottom))
    {
        *map_x = COORD_PER_STL * STL_PER_SLB * (screen_x-map_area.left) / block_size + COORD_PER_STL/2;
        *map_y = COORD_PER_STL * STL_PER_SLB * (screen_y-map_area.top)  / block_size + COORD_PER_STL/2;
        return ((*map_x >= 0) && (*map_x < (game.map_subtiles_x+1)<<8) && (*map_y >= 0) && (*map_y < (game.map_subtiles_y+1)<<8));
    }
    return false;
}

TbBool parchment_copy_background_at(const struct TbRect *bkgnd_area, int units_per_px)
{
    int img_width;
    int img_height;
    unsigned char *srcbuf;
    unsigned char shift;
    if (RendererPhysicalWidth() < 640)
    {
        img_width = 320;
        img_height = 200;
        srcbuf = lores_parchment;
        shift = 5;
    } else
    {
        img_width = 640;
        img_height = 480;
        srcbuf = hires_parchment;
        shift = 4;
    }
    // Only 8bpp supported for now
    if (LbGraphicsScreenBPP() != 8)
        return false;
    // Do the drawing (opaque, game-palette Indexed8 present — parchment background).
    struct RendererPresentImageDesc d = {0};
    d.format  = PRESENT_FORMAT_INDEXED8;
    d.palette = PRESENT_PALETTE_GAME;
    d.kind    = PRESENT_KIND_OPAQUE;
    d.dst_w = img_width*units_per_px/16; d.dst_h = img_height*units_per_px/16;
    d.dst_x = bkgnd_area->left;          d.dst_y = bkgnd_area->top;
    d.src   = srcbuf; d.src_w = img_width; d.src_h = img_height;
    RendererPresentImage(&d);
    // Burning candle flames — submitted via UIRenderer so they composite on top
    // of the GPU parchment blit quad without writing into the CPU staging buffer.
    const struct TbSprite* spr = get_button_sprite(GBS_parchment_map_screen_flame_1 + (get_gameturn() & 3));
    UIRenderer_SubmitScaledSprite(bkgnd_area->left+(36*units_per_px/(pixel_size << shift)),(bkgnd_area->top+0*units_per_px/(16*pixel_size)), spr->SWidth*units_per_px/16, spr->SHeight*units_per_px/16, spr, 0);
    spr = get_button_sprite(GBS_parchment_map_screen_flame_5+(get_gameturn() & 3));
    UIRenderer_SubmitScaledSprite(bkgnd_area->left+(574*units_per_px/(pixel_size << shift)),(bkgnd_area->top+0*units_per_px/(16*pixel_size)), spr->SWidth*units_per_px/16, spr->SHeight*units_per_px/16, spr, 0);
    return true;
}

/**
 * Draws parchment view background, used for in-game level map screen.
 */
void draw_map_parchment(void)
{
    // Get background area rectangle
    struct TbRect bkgnd_area;
    int units_per_px = get_parchment_background_area_rect(&bkgnd_area);
    // Draw it
    parchment_copy_background_at(&bkgnd_area, units_per_px);
    SYNCDBG(9,"Done");
}

TbPixel get_overhead_mapblock_color(MapSubtlCoord stl_x, MapSubtlCoord stl_y, PlayerNumber plyr_idx, TbPixel background)
{
    TbPixel pixval;
    struct Map* mapblk = get_map_block_at(stl_x, stl_y);
    struct SlabMap* slb = get_slabmap_for_subtile(stl_x, stl_y);
    long owner = slabmap_owner(slb);
    if ((((mapblk->flags & SlbAtFlg_Unexplored) != 0) || ((mapblk->flags & SlbAtFlg_TaggedValuable) != 0))
        && ((get_gameturn() % (8 * gui_blink_rate)) >= 4 * gui_blink_rate))
    {
        pixval = pixmap.ghost[background + 0x1A00];
        if (slb->kind == SlbT_GEMS)
        {
            pixval = pixval + 2;
        }
    } else
    if (!map_block_revealed(mapblk,plyr_idx))
    {
        pixval = background;
    } else
    if ((slb->kind == SlbT_GOLD) || (slb->kind == SlbT_DENSEGOLD))
    {
        pixval = pixmap.ghost[background + 0x8C00];
    } else
    if (slb->kind == SlbT_GEMS)
    {
        pixval = 102 + (pixmap.ghost[background] >> 6);
    }
    else if ((mapblk->flags & SlbAtFlg_IsRoom) != 0) // Room slab
    {
        struct Room* room = subtile_room_get(stl_x, stl_y);
        if (((get_gameturn() % (2 * gui_blink_rate)) >= gui_blink_rate) && (room->kind == gui_room_type_highlighted))
        {
            pixval = player_highlight_colours[owner];
      } else
      {
        unsigned char color_idx = get_player_color_idx(owner);
        if (color_idx == PLAYER_NEUTRAL)
        {
            pixval = player_room_colours[(get_gameturn() % (4 * neutral_flash_rate)) / neutral_flash_rate];
        } else
        {
            pixval = player_room_colours[color_idx];
        }
      }

    } else
    {
      if (slb->kind == SlbT_ROCK)
      {
          pixval = 0;
      } else
      if (slb->kind == SlbT_ROCK_FLOOR)
      {
          pixval = pixmap.ghost[3];
      }
      else
      if ((mapblk->flags & SlbAtFlg_Filled) != 0)
      {
          pixval = pixmap.ghost[background + 0x1000];
      } else
      if ((mapblk->flags & SlbAtFlg_IsDoor) != 0) // Door slab
      {
          struct Thing* thing = get_door_for_position(stl_x, stl_y);
          if (thing_is_invalid(thing))
          {
            pixval = 60;
          } else
          if (((get_gameturn() % (2 * gui_blink_rate)) >= gui_blink_rate) && (thing->model == gui_door_type_highlighted))
          {
            pixval = player_highlight_colours[owner];
          } else
          if(door_is_hidden_to_player(thing,plyr_idx))
          {
            pixval = pixmap.ghost[background + 0x1000];
          }else
          if (thing->door.is_locked)
          {
            pixval = 79;
          } else
          {
            pixval = 60;
          }
      } else
      if ((mapblk->flags & SlbAtFlg_Blocking) == 0)
      {
          if (slb->kind == SlbT_LAVA)
          {
            pixval = 146;
          } else
          if (slb->kind == SlbT_WATER)
          {
            pixval = 85;
          } else
          if (slb->kind == SlbT_PURPLE)
          {
              pixval = 255;
          }
          else
          {
            pixval = get_player_path_colour(owner);
          }
        } else
        {
          pixval = background;
        }
    }
    return pixval;
}

void draw_overhead_map(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    // GPU path: build a per-tile RG8 colour buffer.  R = palette index (for
    // direct-colour tiles) or ghost table texture row (for ghost-shaded tiles).
    // G = operation type:
    //   0x00 = unrevealed (transparent — parchment shows through)
    //   0xFF = direct palette lookup on R
    //   0x01 = simple ghost:  palette[ ghost_table[R, parchment_col] ]
    //   0x02 = gems ghost:    palette[ 102 + (ghost_table[64, parchment_col] >> 6) ]
    //   0x03 = blink+gems:    palette[ ghost_table[R, parchment_col] + 2 ]
    // Ghost table rows are offset by +64 in the fade/ghost texture (rows 0-63 = fade).
    int tiles_x = game.map_tiles_x;
    int tiles_y = game.map_tiles_y;
    unsigned char* tile_buf = (unsigned char*)KfxAlloc((size_t)tiles_x * (size_t)tiles_y * 2);
    if (tile_buf)
    {
        long stl_y = 1;
        for (int ty = 0; ty < tiles_y; ty++, stl_y += STL_PER_SLB)
        {
            long stl_x = 1;
            for (int tx = 0; tx < tiles_x; tx++, stl_x += STL_PER_SLB)
            {
                unsigned char r_val = 0;
                unsigned char g_val = 0x00; // default: transparent
                struct Map* mapblk = get_map_block_at(stl_x, stl_y);
                struct SlabMap* slb = get_slabmap_for_subtile(stl_x, stl_y);
                TbBool is_blink = (((mapblk->flags & SlbAtFlg_Unexplored) != 0) || ((mapblk->flags & SlbAtFlg_TaggedValuable) != 0))
                    && ((get_gameturn() % (8 * gui_blink_rate)) >= 4 * gui_blink_rate);
                if (is_blink)
                {
                    // Ghost row 26, offset +64 = texture row 90
                    r_val = 90;
                    g_val = (slb->kind == SlbT_GEMS) ? 0x03 : 0x01;
                }
                else if (!map_block_revealed(mapblk, plyr_idx))
                {
                    g_val = 0x00; // unrevealed — transparent
                }
                else if ((slb->kind == SlbT_GOLD) || (slb->kind == SlbT_DENSEGOLD))
                {
                    // Ghost row 140, offset +64 = texture row 204
                    r_val = 204;
                    g_val = 0x01;
                }
                else if (slb->kind == SlbT_GEMS)
                {
                    // Ghost row 0, offset +64 = texture row 64; gems formula in shader
                    r_val = 64;
                    g_val = 0x02;
                }
                else
                {
                    // All remaining cases: compute on CPU (no ghost-table bg dependency)
                    TbPixel col = get_overhead_mapblock_color(stl_x, stl_y, plyr_idx, 0);
                    if (col == 0 && map_block_revealed(mapblk, plyr_idx))
                    {
                        // SlbT_ROCK returns 0 (true black) — keep it as palette 0
                        r_val = 0;
                        g_val = 0xFF;
                    }
                    else if (col == 0)
                    {
                        g_val = 0x00; // unrevealed/blocking → transparent
                    }
                    else
                    {
                        // Check if this is a ghost-table-dependent result that used bg=0.
                        // Filled/earth and hidden doors use ghost row 16.
                        if ((mapblk->flags & SlbAtFlg_Filled) != 0)
                        {
                            // Ghost row 16, offset +64 = texture row 80
                            r_val = 80;
                            g_val = 0x01;
                        }
                        else if ((mapblk->flags & SlbAtFlg_IsDoor) != 0)
                        {
                            struct Thing* thing = get_door_for_position(stl_x, stl_y);
                            if (!thing_is_invalid(thing) && door_is_hidden_to_player(thing, plyr_idx))
                            {
                                r_val = 80; // ghost row 16
                                g_val = 0x01;
                            }
                            else
                            {
                                r_val = col;
                                g_val = 0xFF;
                            }
                        }
                        else if (slb->kind == SlbT_ROCK_FLOOR)
                        {
                            // ghost[3] is a constant (row 0, col 3) — no bg dependency
                            r_val = col;
                            g_val = 0xFF;
                        }
                        else if ((mapblk->flags & SlbAtFlg_Blocking) != 0)
                        {
                            // Other blocking slabs return background (transparent)
                            g_val = 0x00;
                        }
                        else
                        {
                            r_val = col;
                            g_val = 0xFF;
                        }
                    }
                }
                int idx = (ty * tiles_x + tx) * 2;
                tile_buf[idx + 0] = r_val;
                tile_buf[idx + 1] = g_val;
            }
        }
        if (RendererSubmitOverheadMap(tile_buf, tiles_x, tiles_y,
            map_area->left, map_area->top,
            map_area->right - map_area->left, map_area->bottom - map_area->top))
        {
            KfxFree(tile_buf);
            return;
        }
        KfxFree(tile_buf);
    }

    // CPU fallback: original per-pixel block write to WScreen.
    // Passes *dstbuf (the parchment pixel already in WScreen) as background so
    // the ghost table shading correctly darkens the parchment colour underneath.
    long line = 0;
    long stl_y = 1;
    unsigned char* dstline = &RendererGetWScreen()[map_area->left + RendererScreenWidth() * map_area->top];
    for (long cntr_h = game.map_tiles_y * block_size; cntr_h > 0; cntr_h--)
    {
        if ((line > 0) && ((line % block_size) == 0))
        {
          stl_y += STL_PER_SLB;
        }
        unsigned char* dstbuf = dstline;
        long stl_x = 1;
        for (long cntr_w = game.map_tiles_x; cntr_w > 0; cntr_w--)
        {
            for (long k = block_size; k > 0; k--)
            {
                *dstbuf = get_overhead_mapblock_color(stl_x, stl_y, plyr_idx, *dstbuf);
                dstbuf++;
          }
          stl_x += STL_PER_SLB;
        }
        dstline += RendererScreenWidth();
        line++;
    }
}

void draw_overhead_room_icons(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    int ps_units_per_px;
    {
        const struct TbSprite* spr = get_panel_sprite(GPS_room_treasury_std_s);//only for size, room irrelevant
        ps_units_per_px = 32 * block_size * 4 / spr->SHeight;
    }
    long rkind_select = (get_gameturn() >> 1) % game.conf.slab_conf.room_types_count;
    for (struct Room* room = start_rooms; room < end_rooms; room++)
    {
      if (room_exists(room))
      {
          long room_visibility = labs(rkind_select - room->kind);
          TbBool dimmed = (room_visibility >= 2) && (room_visibility < 4);
          if (room_visibility < 4)
          {
            if (subtile_revealed(room->central_stl_x, room->central_stl_y, plyr_idx))
            {
                const struct RoomConfigStats* roomst = get_room_kind_stats(room->kind);
                if (roomst->medsym_sprite_idx > 0)
                {
                    long sprite_idx = get_player_colored_icon_idx(roomst->medsym_sprite_idx,room->owner);
                    const struct TbSprite* spr = get_panel_sprite(sprite_idx);
                    long pos_x = map_area->left + (block_size * room->central_stl_x / STL_PER_SLB) - (spr->SWidth * ps_units_per_px / 16 / 2);
                    long pos_y = map_area->top + (block_size * room->central_stl_y / STL_PER_SLB) - (spr->SHeight * ps_units_per_px / 16 / 2);
                    if (RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y)) continue;
                    // In GL mode draw via UIRenderer; dimmed rooms are skipped (binary blink vs TRANSPAR4 dim).
                    // In software mode set DrawFlags so LbSpriteDrawResized respects the dimming effect.
                    if (!dimmed) {
                        UIRenderer_SubmitPanelSpriteRaw(pos_x, pos_y, ps_units_per_px, spr, 0);
                    } else {
                        UIRenderer_SubmitPanelSpriteRaw(pos_x, pos_y, ps_units_per_px, spr, Lb_SPRITE_TRANSPAR4);
                    }
                }
            }
          }
        }
    }
}

int draw_overhead_call_to_arms(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    int n = 0;
    for (int i = 0; i < DUNGEONS_COUNT; i++)
    {
        if (player_uses_power_call_to_arms(i))
        {
            struct Dungeon* dungeon = get_dungeon(i);
            const struct PowerConfigStats *powerst = get_power_model_stats(PwrK_CALL2ARMS);
            long m = (4 * ((i + get_gameturn()) & 7) * subtile_slab(powerst->strength[dungeon->cta_power_level]));
            long pos_x = map_area->left + block_size * (int)dungeon->cta_stl_x / STL_PER_SLB;
            long pos_y = map_area->top + block_size * (int)dungeon->cta_stl_y / STL_PER_SLB;
            long radius = (((m & 7) + m) >> 3) / pixel_size;
            unsigned char col = player_room_colours[get_player_color_idx(i)];
            if (RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y)) { n++; continue; }
            // GPU path: approximate pulsing circle as an outline rectangle.
            // Software path: LbDrawCircle via DrawFlags=Lb_SPRITE_OUTLINE.
            UIRenderer_SubmitOutlineBox(pos_x - radius, pos_y - radius, radius * 2, radius * 2, col);
            n++;
        }
    }
    return n;
}

int draw_overhead_creatures(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    TbPixel col;
    short pixel_end;
    int p;
    int n = 0;
    int k = 0;
    const struct StructureList* slist = get_list_for_thing_class(TCls_Creature);
    int i = slist->index;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
          ERRORLOG("Jump to invalid thing detected");
          break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (!thing_is_picked_up(thing))
        {
            unsigned char color_idx = get_player_color_idx(thing->owner);
            TbPixel col1 = player_highlight_colours[color_idx];
            TbPixel col2 = 1;
            if (thing_revealed(thing, plyr_idx))
            {
                if (color_idx == game.neutral_player_num)
                {
                    col1 = player_room_colours[(((get_gameturn() + neutral_flash_rate) % (4 * neutral_flash_rate)) / neutral_flash_rate)];
                } else
                if ((get_gameturn() % (8 * gui_blink_rate)) < 4 * gui_blink_rate)
                {
                    col2 = player_room_colours[color_idx];
                    col1 = player_room_colours[color_idx];
                }
                long pos_x = map_area->left + block_size * (int)thing->mappos.x.stl.num / STL_PER_SLB;
                long pos_y = map_area->top + block_size * (int)thing->mappos.y.stl.num / STL_PER_SLB;
                if (thing->owner == plyr_idx)
                    col = col2;
                else
                    col = col1;
                if (!RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y))
                {
                    pixel_end = get_pixels_scaled_and_zoomed(TWO_PIXELS);
                    for (p = 0; p < pixel_end; p++)
                    {
                        UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, col);
                    }
                    n++;
                }
            } else
            // Special tunneler code
            if (is_hero_tunnelling_to_attack(thing))
            {
                struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
                if ((get_gameturn() % (8 * gui_blink_rate)) < 4 * gui_blink_rate)
                {
                    col1 = player_room_colours[get_player_color_idx((int)(cctrl->party.target_plyr_idx>=0?cctrl->party.target_plyr_idx:0))];
                    col2 = player_room_colours[get_player_color_idx(thing->owner)];
                }
                if (thing->owner == plyr_idx)
                {
                    col = col2;
                }
                else
                {
                    col = col1;
                }
                for (int m = 0; m < 5; m++)
                {
                    long memberpos = cctrl->party.member_pos_stl[m];
                    if (memberpos == 0)
                        break;
                    long pos_x = map_area->left + block_size * stl_num_decode_x(memberpos) / STL_PER_SLB;
                    long pos_y = map_area->top + block_size * stl_num_decode_y(memberpos) / STL_PER_SLB;
                    if (RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y)) { n++; continue; }
                    pixel_end = get_pixels_scaled_and_zoomed(TWO_PIXELS);
                    for (p = 0; p < pixel_end; p++)
                    {
                        UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, col);
                    }
                    n++;
                }
            }
        }
        // Per-thing code ends
        k++;
        if (k > THINGS_COUNT)
        {
          ERRORLOG("Infinite loop detected when sweeping things list");
          break;
        }
    }
    return n;
}

int draw_overhead_traps(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    int n = 0;
    int k = 0;
    const struct StructureList* slist = get_list_for_thing_class(TCls_Trap);
    int i = slist->index;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
          ERRORLOG("Jump to invalid thing detected");
          break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (!thing_is_picked_up(thing))
        {
            if (thing->owner == plyr_idx)
            {
                if ( (thing->trap.revealed) || (thing->owner == plyr_idx) )
                {
                    long pos_x = map_area->left + (block_size * (int)thing->mappos.x.stl.num / STL_PER_SLB) + ((block_size + 1)/5);
                    long pos_y = map_area->top + (block_size * (int)thing->mappos.y.stl.num / STL_PER_SLB) + ((block_size + 1)/5);
                    if (!RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y))
                    {
                    short pixels_amount = scale_pixel(ONE_PIXEL);
                    short pixel_end = get_pixels_scaled_and_zoomed(ONE_PIXEL);
                    short colour = 60;
                    for (int p = 0; p < pixel_end; p++)
                    {
                        // Draw a cross
                        UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, colour);
                        UIRenderer_SubmitSolidBox(pos_x + pixels_amount + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, colour);
                        UIRenderer_SubmitSolidBox(pos_x - pixels_amount + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, colour);
                        UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + pixels_amount + draw_square[p].delta_y, 1, 1, colour);
                        UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y - pixels_amount + draw_square[p].delta_y, 1, 1, colour);
                    }
                    n++;
                    }
                }
            }
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
          ERRORLOG("Infinite loop detected when sweeping things list");
          break;
        }
    }
    return n;
}

int draw_overhead_spells(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    int n = 0;
    const struct StructureList* slist = get_list_for_thing_class(TCls_Object);
    int k = 0;
    int i = slist->index;
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
          ERRORLOG("Jump to invalid thing detected");
          break;
        }
        i = thing->next_of_class;
        // Per-thing code
        if (!thing_is_picked_up(thing))
        {
            if (thing_revealed(thing, plyr_idx))
            {
              if ( thing_is_special_box(thing) || thing_is_spellbook(thing) )
              {
                  long pos_x = map_area->left + block_size * (int)thing->mappos.x.stl.num / STL_PER_SLB  + ((block_size + 1)/5);
                  long pos_y = map_area->top + block_size * (int)thing->mappos.y.stl.num / STL_PER_SLB + ((block_size + 1)/5);
                  if (!RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y)) {
                  short pixel_end = get_pixels_scaled_and_zoomed(TWO_PIXELS);
                  for (int p = 0; p < pixel_end; p++)
                  {
                      UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, colours[15][0][15]);
                  }
                  }
              }
              else if ( thing_is_workshop_crate(thing) )
              {
                  long pos_x = map_area->left + block_size * (int)thing->mappos.x.stl.num / STL_PER_SLB  + ((block_size + 1)/5);
                  long pos_y = map_area->top + block_size * (int)thing->mappos.y.stl.num / STL_PER_SLB + ((block_size + 1)/5);
                  if (!RendererPointInZoomBoxScreenRect((int)pos_x, (int)pos_y)) {
                  short pixel_end = get_pixels_scaled_and_zoomed(TWO_PIXELS);
                  for (int p = 0; p < pixel_end; p++)
                  {
                      UIRenderer_SubmitSolidBox(pos_x + draw_square[p].delta_x, pos_y + draw_square[p].delta_y, 1, 1, colours[7][6][7]);
                  }
                  }
              }
            }
        }
        // Per-thing code ends
        k++;
        if (k > slist->count)
        {
          ERRORLOG("Infinite loop detected when sweeping things list");
          break;
        }
    }
    return n;
}

void draw_overhead_things(const struct TbRect *map_area, long block_size, PlayerNumber plyr_idx)
{
    draw_overhead_creatures(map_area, block_size, plyr_idx);
    draw_overhead_call_to_arms(map_area, block_size, plyr_idx);
    if ((((get_gameturn()) % (4 * gui_blink_rate)) / gui_blink_rate) == 1) {
        draw_overhead_spells(map_area, block_size, plyr_idx);
    }
    draw_overhead_traps(map_area, block_size, plyr_idx);
}

void draw_2d_map(void)
{
    SYNCDBG(8, "Starting");
    if (!render_fade_tables)
    {
        render_fade_tables = pixmap.fade_tables;
    }
    struct PlayerInfo* player = get_my_player();
    // Size of the parchment map on which we're drawing
    struct TbRect map_area;
    long block_size = get_parchment_map_area_rect(&map_area);
    // Now draw
    draw_overhead_map(&map_area, block_size, player->id_number);
    draw_overhead_things(&map_area, block_size, player->id_number);
    draw_overhead_room_icons(&map_area, block_size, player->id_number);
}

void draw_map_level_name(void)
{
    // Retrieving name
    const char* lv_name = NULL;
    LevelNumber lvnum = get_loaded_level_number();
    struct LevelInformation* lvinfo = get_level_info(lvnum);
    if (lvinfo != NULL)
    {
      if (lvinfo->name_stridx > 0)
        lv_name = get_string(lvinfo->name_stridx);
      else
        lv_name = lvinfo->name;
    } else
    if (is_multiplayer_level(lvnum))
    {
      lv_name = level_name;
    }
    // Retrieving position
    struct TbRect bkgnd_area;
    get_parchment_background_area_rect(&bkgnd_area);
    // Set position
    LbTextSetFont(winfont);
    int x = bkgnd_area.left;
    int y = bkgnd_area.top;
    int w = bkgnd_area.right - bkgnd_area.left;
    int h = (bkgnd_area.bottom - bkgnd_area.top) / 4;
    // Drawing
    if (lv_name != NULL)
    {
        LbTextSetWindow(x, y, w, h);
        int tx_units_per_px = ( (RendererGetScreenHeight() < 400) && (dbc_language > 0) ) ? scale_ui_value(32) : (22 * units_per_pixel) / LbTextLineHeight();
        LbTextDrawResized((w-LbTextStringWidth(lv_name)*units_per_pixel/16)/2, h/10 - 8*units_per_pixel/16, tx_units_per_px, lv_name, 0);
    }
}

void draw_zoom_box_things_on_mapblk(struct Map *mapblk,unsigned short subtile_size,int scr_x,int scr_y)
{
    int ps_units_per_px;
    {
        const struct TbSprite* spr = get_panel_sprite(GPS_trapdoor_bonus_box_std_s); // Use dungeon special box as reference
        ps_units_per_px = (46 * units_per_pixel) / spr->SHeight;
    }
    struct PlayerInfo* player = get_my_player();
    unsigned long k = 0;
    struct ObjectConfigStats* objst;
    long i = get_mapwho_thing_index(mapblk);
    while (i != 0)
    {
        struct Thing* thing = thing_get(i);
        if (thing_is_invalid(thing))
        {
            WARNLOG("Jump out of things array");
            break;
        }
        i = thing->next_on_mapblk;
        if (!thing_is_picked_up(thing))
        {
            int spos_x = ((subtile_size * ((long)thing->mappos.x.stl.pos)) >> 8);
            int spos_y = ((subtile_size * ((long)thing->mappos.y.stl.pos)) >> 8);
            long spridx;
            switch (thing->class_id)
            {
            case TCls_Creature:
            {
                spridx = get_creature_model_graphics(thing->model, CGI_HandSymbol);
                if ((get_gameturn() % (8 * gui_blink_rate)) >= 4 * gui_blink_rate)
                {
                    TbPixel color = get_player_path_colour(thing->owner);
                    draw_gui_panel_sprite_occentered(scr_x + spos_x, scr_y + spos_y - 13*units_per_pixel/16, ps_units_per_px, spridx, color, 0);
                } else
                {
                    draw_gui_panel_sprite_centered(scr_x + spos_x, scr_y + spos_y - 13*units_per_pixel/16, ps_units_per_px, spridx, 0);
                }
                draw_status_sprites(spos_x + scr_x, scr_y + spos_y - 12*units_per_pixel/16, thing);
                break;
            }
            case TCls_Trap:
            {
                struct TrapConfigStats* trapst = get_trap_model_stats(thing->model);
                if ((!thing->trap.revealed) && (player->id_number != thing->owner) && (trapst->hidden != 0))
                    break;
                struct ManufactureData* manufctr = get_manufacture_data(get_manufacture_data_index_for_thing(thing->class_id, thing->model));
                spridx = manufctr->medsym_sprite_idx;
                //This line and all cases below used to be: draw_gui_panel_sprite_centered(scr_x + spos_x, scr_y + spos_y - 13*units_per_pixel/16, ps_units_per_px, spridx, 0);
                draw_gui_panel_sprite_centered(scr_x + (spos_x * 3 / 2), scr_y - (spos_y /2), ps_units_per_px, spridx, 0);
                break;
            }
            case TCls_Object:
                //get spridx from config
                objst = get_object_model_stats(thing->model);
                spridx = objst->map_icon;
                if (spridx < 0)
                {
                    if (thing_is_spellbook(thing))
                    {
                        struct PowerConfigStats* powerst;
                        powerst = get_power_model_stats(book_thing_to_power_kind(thing));
                        spridx = powerst->medsym_sprite_idx;
                    }
                }
                if (spridx > 0)
                {
                    draw_gui_panel_sprite_centered(spos_x + scr_x, scr_y + spos_y - 6 * units_per_pixel / 16, ps_units_per_px, spridx, 0);
                }
                break;
            default:
                break;
            }
        }
        k++;
        if (k > THINGS_COUNT)
        {
            ERRORLOG("Infinite loop detected when sweeping things list");
            break_mapwho_infinite_chain(mapblk);
            break;
        }
    }
}

void draw_zoom_box_terrain(long scrtop_x, long scrtop_y, int stl_x, int stl_y, PlayerNumber plyr_idx, long draw_tiles_x, long draw_tiles_y, int subtile_size)
{
    scrtop_x += 4*units_per_pixel/16;
    scrtop_y -= 4*units_per_pixel/16;

    // Build a per-subtile texture-block index buffer and let the active renderer
    // materialise it (GPU as textured quads, software via draw_texture).
    unsigned short* tile_buf = (unsigned short*)KfxAlloc((size_t)draw_tiles_x * (size_t)draw_tiles_y * sizeof(unsigned short));
    if (tile_buf)
    {
        for (int dy = 0; dy < draw_tiles_y; dy++)
            for (int dx = 0; dx < draw_tiles_x; dx++)
            {
                struct Map* mapblk = get_map_block_at(stl_x + dx, stl_y + dy);
                if (map_block_revealed(mapblk, plyr_idx))
                {
                    int k = element_top_face_texture(mapblk);
                    k = engine_remap_texture_blocks(stl_x + dx, stl_y + dy, k);
                    tile_buf[dy * draw_tiles_x + dx] = (unsigned short)k;
                }
                else
                {
                    tile_buf[dy * draw_tiles_x + dx] = 0xFFFF;  // unrevealed — show background
                }
            }
        RendererSubmitZoomBoxTiles(tile_buf, draw_tiles_x, draw_tiles_y,
            scrtop_x, scrtop_y, subtile_size, subtile_size);
        KfxFree(tile_buf);
        return;
    }

    // Software path: direct pixel write via the SW rasteriser.
    // Only reachable on tile_buf allocation failure (OOM). In GL mode there is no
    // CPU staging buffer (RendererGetWScreen() is always NULL — same class of bug
    // as draw_slab64k_background); setup_vecs() would silently leave vec_screen
    // unset, and draw_texture() below writes through it directly. Bail out instead.
    if (RendererGetWScreen() == NULL)
    {
        WARNLOG("Zoom box tile buffer allocation failed and no CPU screen buffer is available; skipping terrain fallback draw");
        return;
    }
    setup_vecs(RendererGetWScreen(), NULL, (unsigned int)RendererScreenWidth(), (unsigned int)RendererScreenWidth(), (unsigned int)RendererScreenHeight());
    int scr_y = scrtop_y;
    for (int map_dy = 0; map_dy < draw_tiles_y; map_dy++)
    {
        int scr_x = scrtop_x;
        for (int map_dx = 0; map_dx < draw_tiles_x; map_dx++)
        {
            struct Map* mapblk = get_map_block_at(stl_x + map_dx, stl_y + map_dy);
            if (map_block_revealed(mapblk, plyr_idx))
            {
                int k = element_top_face_texture(mapblk);
                k = engine_remap_texture_blocks(stl_x + map_dx, stl_y + map_dy, k);
                draw_texture(scr_x, scr_y, subtile_size, subtile_size, k, 0, -1);
            } else
          {
            LbDrawBox(scr_x, scr_y, subtile_size, subtile_size, 1, 0);
          }
          scr_x += subtile_size;
      }
      scr_y += subtile_size;
    }
    LbDrawBox(scrtop_x, scrtop_y, draw_tiles_x*subtile_size, draw_tiles_y*subtile_size, 0, Lb_SPRITE_OUTLINE);
}

void draw_zoom_box_things(long scrtop_x, long scrtop_y, int stl_x, int stl_y, PlayerNumber plyr_idx, long draw_tiles_x, long draw_tiles_y, int subtile_size)
{
    UIRenderer_BeginZoomBoxOverlay(scrtop_x, scrtop_y,
        draw_tiles_x * subtile_size, draw_tiles_y * subtile_size);
    int scr_y = scrtop_y - RendererGraphicsWindowY();
    for (int map_dy = 0; map_dy < draw_tiles_y; map_dy++)
    {
        int scr_x = scrtop_x - RendererGraphicsWindowX();
        for (int map_dx = 0; map_dx < draw_tiles_x; map_dx++)
        {
            struct Map* mapblk = get_map_block_at(stl_x + map_dx, stl_y + map_dy);
            if (map_block_revealed(mapblk, plyr_idx))
            {
                draw_zoom_box_things_on_mapblk(mapblk, subtile_size, scr_x, scr_y);
            }
            scr_x += subtile_size;
      }
      scr_y += subtile_size;
    }
    UIRenderer_EndZoomBoxOverlay(scrtop_x, scrtop_y,
        draw_tiles_x * subtile_size, draw_tiles_y * subtile_size);
}

void redraw_minimal_overhead_view(void)
{
    draw_map_parchment();
    draw_2d_map();
    draw_gui();
    draw_tooltip();
}

void zoom_to_parchment_map(void)
{
    turn_off_all_window_menus();
    if ((game.operation_flags & GOF_ShowGui) == 0)
      clear_flag(game.operation_flags, GOF_ShowPanel);
    else
      set_flag(game.operation_flags, GOF_ShowPanel);
    // Note: turn_off_all_window_menus() already deactivates GMnu_MAIN.
    // toggle_status_menu(0) must NOT be called here -- it would see GMnu_MAIN
    // as already-off (turn_off already ran) and would incorrectly clear
    // GOF_ShowPanel, breaking the restore path in set_player_mode(PVT_DungeonTop).
    // The draw_gui()/sidebar content that used to need this suppression no
    // longer runs in parchment view (removed from ParchmentScene::draw()).
    struct PlayerInfo* player = get_my_player();
    if (network_is_active()
        || (!MapFadePass_SupportsNativeResolution() && RendererPhysicalWidth() > 320))
    {
      set_players_packet_action(player, PckA_SaveViewType, PVT_MapScreen, 0, 0, 0);
      turn_off_roaming_menus();
    } else
    {
      set_players_packet_action(player, PckA_SetViewType, PVT_MapFadeIn, 0, 0, 0);
      turn_off_roaming_menus();
    }
}

void zoom_from_parchment_map(void)
{
    struct PlayerInfo* player = get_my_player();
    // Restoration of GMnu_MAIN is handled by set_player_mode(PVT_DungeonTop),
    // which calls toggle_status_menu((GOF_ShowPanel) != 0). Calling it here
    // prematurely restores the sidebar mid-fade (visual artefact) and is
    // unnecessary since ParchmentScene no longer calls draw_gui() at all.
    if (network_is_active()
        || (!MapFadePass_SupportsNativeResolution() && RendererPhysicalWidth() > 320))
    {
        set_players_packet_action(player, PckA_LoadViewType, PVT_DungeonTop, 0,0,0);
    } else
    {
        set_players_packet_action(player, PckA_SetViewType, PVT_MapFadeOut, 0,0,0);
    }
}
/******************************************************************************/
