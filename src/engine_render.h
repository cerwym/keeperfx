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

#define STRIPEY_COLORS  16  // number of palette entries per stripey line cycle
#define LINE_BOX_SCALE  100 // line_box_size is stored as a percentage; divide by this to get multiplier

struct stripey_line {
    TbPixel stripey_line_color_array[STRIPEY_COLORS];
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

/* Viewport dimensions set by the engine rendering pipeline.
 * These are shared with hardware backends for projection. */
extern long vec_window_width;
extern long vec_window_height;

extern long z_threshold_near;
extern long split_2;
extern long fade_max;
/** Fog-of-war lighting lookup table. Set by engine init after table load. */
extern unsigned char *render_fade_tables;

/* Sprite scaling state captured by LbSpriteSetScalingData —
 * read by hardware backends via the keeper-sprite hook. */
extern long g_sprite_scale_dst_x;
extern long g_sprite_scale_dst_y;
extern long g_sprite_scale_dst_w;
extern long g_sprite_scale_dst_h;
extern long g_sprite_scale_src_w;
extern long g_sprite_scale_src_h;

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

// Orient-to-UV lookup tables used by draw_texturedquad_block() / append_frontview_quad().
// Values are 16:16 fixed-point (0 or 0x1F0000 = 31.0); shift right 16 to get 0 or 31.
extern long const orient_to_mapU1[4];
extern long const orient_to_mapU2[4];
extern long const orient_to_mapU3[4];
extern long const orient_to_mapU4[4];
extern long const orient_to_mapV1[4];
extern long const orient_to_mapV2[4];
extern long const orient_to_mapV3[4];
extern long const orient_to_mapV4[4];

extern unsigned char temp_cluedo_mode;
/******************************************************************************/

extern TbSpriteData keepersprite_add[KEEPERSPRITE_ADD_NUM];
extern short iso_td_add[KEEPERSPRITE_ADD_NUM];
extern short td_iso_add[KEEPERSPRITE_ADD_NUM];

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

/** process_keeper_sprite wrapper that sets draw globals to specified values,
 *  calls process_keeper_sprite, then restores them.
 *  Allows renderer code to replay captured draw state without touching lbDisplay
 *  or EngineSpriteDrawUsingAlpha directly.
 *  @param draw_flags  lbDisplay.DrawFlags value to set during the call.
 *  @param alpha       EngineSpriteDrawUsingAlpha value to set during the call. */
void process_keeper_sprite_ex(short x, short y, unsigned short kspr_base,
                               short kspr_angle, unsigned char sprgroup, long scale,
                               unsigned int draw_flags, unsigned char alpha);

/** Snapshot of engine projection and window globals saved before a temporary
 *  re-render (e.g. GLMapFadePass FBO capture) and restored afterwards.
 *  All fields must remain valid between save and restore. */
struct EngineRenderState {
    long    vec_w;       /* vec_window_width  */
    long    vec_h;       /* vec_window_height */
    Offset  vert[3];     /* vert_offset[0..2] */
    Offset  hori[3];     /* hori_offset[0..2] */
    long    x_init;      /* x_init_off        */
    long    y_init;      /* y_init_off        */
    /* Engine window rect (saved/restored via store/setup_engine_window). */
    long    ewnd_x;
    long    ewnd_y;
    long    ewnd_w;
    long    ewnd_h;
};

/** Save the engine projection/window state into @p s.
 *  Does NOT call store_engine_window internally — the ewnd_* fields must be
 *  filled by the caller via store_engine_window if needed, OR use
 *  engine_save_render_state_full() which saves the engine window too. */
void engine_save_render_state(struct EngineRenderState *s);

/** Restore the engine projection/window state from @p s. */
void engine_restore_render_state(const struct EngineRenderState *s);
void draw_status_sprites(long a1, long a2, struct Thing *thing);
void draw_map_volume_box(long cor1_x, long cor1_y, long cor2_x, long cor2_y, long floor_height_z, unsigned char color);

void update_engine_settings(struct PlayerInfo *player);
void draw_view(struct Camera *cam, unsigned char a2);
void draw_frontview_engine(struct Camera *cam);

/** Bucket-list draw — called by SoftwareWorldViewRenderer. */
void display_drawlist(void);
void display_drawlist_sprites_only(void);
void display_fast_drawlist(struct Camera *cam);

/** Draw only depth-positioned 3D entity sprites for one bucket.
 *  Called by GLWorldViewRenderer between gpu_flush() and RenderPass_DrawNow(). */
void draw_3d_sprites_for_bucket(long bucket_num);

/** Front-view equivalent of draw_3d_sprites_for_bucket().
 *  Calls draw_fastview_mapwho() instead of draw_jonty_mapwho(). */
void draw_frontview_3d_sprites_for_bucket(long bucket_num, struct Camera *cam);
void draw_frontview_3d_sprites_for_bucket_current(long bucket_num);

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
