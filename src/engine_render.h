/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_render.h
 *     Header file for engine_render.c.
 * @par Purpose:
 *     Rendering the 3D view functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     20 Mar 2009 - 30 Mar 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_ENGNREND_H
#define DK_ENGNREND_H

#include "bflib_basics.h"
#include "globals.h"
#include "game_legacy.h"
#include "bflib_render.h"
#include "bflib_sprite.h"
#include "engine_lenses.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define POLY_POOL_SIZE 16777216 // Originally 262144, adjusted for view distance
#define Z_DRAW_DISTANCE_MAX 65536 // Originally 11232, adjusted for view distance
#define BUCKETS_COUNT 4098 // Originally 704, adjusted for view distance. (65536/16)+2
#define BUCKETS_STEP 16 // Bucket size in Z steps

#define KEEPSPRITE_LENGTH 9149
#define KEEPERSPRITE_ADD_OFFSET 16384
#define KEEPERSPRITE_ADD_NUM 16383

// Depth calculation function for consistent GPU depth buffer usage
float calculate_normalized_depth(long z_value);
float bucket_index_to_normalized_depth(long bucket_idx);

struct EngineCoord { // sizeof = 28
  long view_width; // X screen position, probably not a width
  long view_height; // Y screen position, probably not a height
  unsigned short clip_flags; // Clipping and culling flags for frustum culling
  unsigned short shade_intensity; // Shading intensity for vertex lighting
  long render_distance; // Distance used for rendering calculations
  long x;
  long y;
  long z;
};

struct M31 {
    long v[4];
};

struct M33 { // sizeof = 48
    struct M31 r[3];
};

struct MapVolumeBox { // sizeof = 24
  unsigned char visible;
  unsigned char color;
  long beg_x;
  long beg_y;
  long end_x;
  long end_y;
  long floor_height_z;
};

/******************************************************************************/
// Stripey Line Color Arrays

enum stripey_line_colors {
    SLC_RED = 0, // INVALID SELECTION
    SLC_GREEN = 1, // VALID SELECTION
    SLC_YELLOW,
    SLC_BROWN,
    SLC_GREY,
    SLC_REDYELLOW,
    SLC_GREENFLASH,
    SLC_REDFLASH,
    SLC_PURPLE,
    SLC_BLUE,
    SLC_ORANGE,
    SLC_WHITE,
    SLC_GREEN2,
    SLC_DARKGREEN,
    SLC_MIXEDGREEN,
    STRIPEY_LINE_COLOR_COUNT // Must always be the last entry (add new colours above this line)
};

struct stripey_line {
    TbPixel stripey_line_color_array[16];
    unsigned int line_color;
};

extern struct stripey_line colored_stripey_lines[];
extern unsigned char *poly_pool;
extern unsigned char *poly_pool_end;
extern long cells_away;
extern float hud_scale;
extern int creature_status_size;
extern int line_box_size;

extern struct MapVolumeBox map_volume_box;
extern long view_height_over_2;
extern long view_width_over_2;
extern long z_threshold_near;
extern long split_2;
extern long fade_max;

extern short mx;
extern short my;
extern short mz;

extern long floor_pointed_at_x;
extern long floor_pointed_at_y;
extern long box_lag_compensation_x;
extern long box_lag_compensation_y;
extern Offset vert_offset[3];
extern Offset hori_offset[3];
extern Offset high_offset[3];

extern TbSpriteData *keepsprite[KEEPSPRITE_LENGTH];
extern TbSpriteData sprite_heap_handle[KEEPSPRITE_LENGTH];
extern struct HeapMgrHeader *graphics_heap;
extern TbFileHandle jty_file_handle;

extern long x_init_off;
extern long y_init_off;
extern struct Thing *thing_being_displayed;

extern unsigned char temp_cluedo_mode;
/******************************************************************************/

extern TbSpriteData keepersprite_add[KEEPERSPRITE_ADD_NUM];

/** Try to submit a keeper sprite through IWorldViewRenderer::SubmitKeeperSprite.
 *  Returns 1 if successfully submitted (CPU blit skipped), 0 to fall back. */
int try_submit_keepersprite_to_render_system(long screen_x, long screen_y, long screen_w, long screen_h,
                                           const unsigned char *sprite_data, int src_w, int src_h,
                                           unsigned int draw_flags, const unsigned char *remap);

/*****************************************************************************/
float interpolate(float variable_to_interpolate, long previous, long current);
float interpolate_angle(float variable_to_interpolate, float previous, float current);

int floor_height_for_volume_box(PlayerNumber plyr_idx, MapSlabCoord slb_x, MapSlabCoord slb_y);
void frame_wibble_generate(void);
void setup_rotate_stuff(long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8);

void process_keeper_sprite(short x, short y, unsigned short a3, short kspr_angle, unsigned char a5, long a6);
void draw_status_sprites(long a1, long a2, struct Thing *thing);
void draw_map_volume_box(long cor1_x, long cor1_y, long cor2_x, long cor2_y, long floor_height_z, unsigned char color);

void update_engine_settings(struct PlayerInfo *player);
void draw_view(struct Camera *cam, unsigned char a2);
void draw_frontview_engine(struct Camera *cam);

/** Bucket-list flush — called by SoftwareWorldViewRenderer. */
void display_drawlist(void);
void display_drawlist_sprites_only(void);
void display_fast_drawlist(struct Camera *cam);

/** Draw only depth-positioned 3D entity sprites for one bucket.
 *  Called by GLWorldViewRenderer between gpu_flush() and RenderPass_FlushNow(). */
void draw_3d_sprites_for_bucket(long bucket_num);

/** Draw all non-spatial sprites (shadows, selector, status, text, room flags)
 *  across all buckets into the CPU staging buffer. */
void draw_nonspatial_sprites(void);

/** Same as draw_nonspatial_sprites() but skips QK_CreatureShadow entries.
 *  Used by the GL renderer which handles shadows via its own GPU path. */
void draw_nonspatial_sprites_no_shadows(void);

/** GPU-accelerated version of draw_nonspatial_sprites_no_shadows().
 *  Submits UI elements to the hardware renderer for GPU batching instead of CPU rasterization. */
void draw_nonspatial_sprites_gpu(void);

/** Rasterize a keeper sprite frame into a 256×256 byte scratch buffer.
 *  Non-zero bytes indicate shadow silhouette pixels. */
void draw_keepsprite_unscaled_in_buffer(unsigned short kspr_n, short angle, unsigned char current_frame, unsigned char *outbuf);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
