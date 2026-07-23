/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_render_tmap.c
 *     Scaled texture-block blitter for the software rasteriser.
 * @par Purpose:
 *     Draws a 32x32 texture block scaled into an arbitrary destination rect,
 *     with optional orientation (8 flag variants) and palette fade.
 *     Blocks live in a shared texture page 8 blocks (256 texels) wide, so
 *     stepping one texel row within a block advances 256 bytes.
 * @par Comment:
 *     A rasterisation primitive: it belongs to the software renderer, not to
 *     game code..
 *
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"     // pixel_size
#include "bflib_vidraw.h"    // vec_screen, vec_screen_width, vec_window_*, draw_texture
#include "engine_textures.h" // block_ptrs
#include "vidfade.h"         // pixmap.fade_tables
#include "post_inc.h"

/******************************************************************************/

/** Texture coordinates are 16.16 fixed point (low 16 bits fractional).  A block
 *  axis is 32 texels (block_dimension), so a full traversal spans
 *  (32 << 16) - 1 = 0x1FFFFF — deliberately one ulp short of 32.0 so the integer
 *  part tops out at 31 and never steps into the neighbouring block. */
#define TMAP_AXIS_SPAN 2097151

/** Per-destination-pixel source-coordinate tables, sized for the original
 *  640x480 display.  These bound the destination rect of a SINGLE block draw,
 *  not the screen resolution. They are enforced below because
 *  the only clamp available (vec_window_*) is the whole screen, which exceeds
 *  these at any modern resolution. */
#define TMAP_WLIMITS_MAX 640
#define TMAP_HLIMITS_MAX 480

/** Blit texture block `texture_block_index` scaled to (scaled_width x scaled_height)
 *  at (screen_x, screen_y) in rasteriser-target space.
 *  `flags` selects one of 8 orientations (0x00..0x70); `fade_level` >= 0 applies
 *  the matching pixmap.fade_tables row, < 0 draws unfaded. */
static void scale_tmap2(int32_t texture_block_index, int32_t flags, int32_t fade_level,
                        int32_t screen_x, int32_t screen_y,
                        int32_t scaled_width, int32_t scaled_height)
{
    if ((scaled_width == 0) || (scaled_height == 0)) {
        return;
    }
    // The eight legal flags values are exactly the combinations of three independent bits
    if ((flags & ~TMAP_ORIENT_MASK) != 0) {
        return;
    }

    // Steps are derived from the REQUESTED size, before the clipping below, so
    // clipping shifts the sampled window instead of stretching the texture.
    const int32_t x_step = TMAP_AXIS_SPAN / scaled_width;
    const int32_t y_step = TMAP_AXIS_SPAN / scaled_height;
    const int32_t orient = ((flags & TMAP_TRANSPOSE) != 0) ? 1 : 0;
    int32_t xstart = ((flags & TMAP_FLIP_X) != 0) ? TMAP_AXIS_SPAN : 0;
    int32_t ystart = ((flags & TMAP_FLIP_Y) != 0) ? TMAP_AXIS_SPAN : 0;
    int32_t xend   = ((flags & TMAP_FLIP_X) != 0) ? -x_step : x_step;
    int32_t yend   = ((flags & TMAP_FLIP_Y) != 0) ? -y_step : y_step;
    const int32_t window_width  = (int32_t)vec_window_width;
    const int32_t window_height = (int32_t)vec_window_height;
    const int32_t dst_stride    = (int32_t)vec_screen_width;
    int32_t local_screen_x;
    int32_t local_screen_y;
    local_screen_x = screen_x;
    if (local_screen_x < 0)
    {
        scaled_width += local_screen_x;
        if (scaled_width < 0) {
            return;
        }
        xstart -= xend * local_screen_x;
        local_screen_x = 0;
    }
    if (local_screen_x + scaled_width > window_width)
    {
        scaled_width = window_width - local_screen_x;
        if (scaled_width < 0) {
            return;
        }
    }
    local_screen_y = screen_y;
    if (local_screen_y < 0)
    {
        scaled_height += local_screen_y;
        if (scaled_height < 0) {
            return;
        }
        ystart -= local_screen_y * yend;
        local_screen_y = 0;
    }
    if (local_screen_y + scaled_height > window_height)
    {
        scaled_height = window_height - local_screen_y;
        if (scaled_height < 0) {
            return;
        }
    }
    // Guard the fixed-size limit tables.
    {
        const int32_t max_w = orient ? TMAP_HLIMITS_MAX : TMAP_WLIMITS_MAX;
        const int32_t max_h = orient ? TMAP_WLIMITS_MAX : TMAP_HLIMITS_MAX;
        if (scaled_width  > max_w) scaled_width  = max_w;
        if (scaled_height > max_h) scaled_height = max_h;
    }

    int32_t i;
    int32_t hlimits[TMAP_HLIMITS_MAX];
    int32_t wlimits[TMAP_WLIMITS_MAX];
    int32_t *xlim;
    int32_t *ylim;
    unsigned char *dbuf;
    unsigned char *block;
    if (!orient)
    {
        xlim = wlimits;
        for (i = scaled_width; i > 0; i--)
        {
            *xlim = xstart;
            xlim++;
            xstart += xend;
        }
        ylim = hlimits;
        for (i = scaled_height; i > 0; i--)
        {
            *ylim = ystart;
            ylim++;
            ystart += yend;
        }
        dbuf = &vec_screen[local_screen_x + local_screen_y * dst_stride];
        block = block_ptrs[texture_block_index];
        ylim = hlimits;
        int32_t px;
        int32_t py;
        int32_t srcx;
        int32_t srcy;
        unsigned char *d;
        if ( fade_level >= 0 )
        {
          for (py = scaled_height; py > 0; py--)
          {
              xlim = wlimits;
              d = dbuf;
              srcy = (((*ylim) & 0xFF0000u) >> 16);
              for (px = scaled_width; px > 0; px--)
              {
                srcx = (((*xlim) & 0xFF0000u) >> 16);
                xlim++;
                *d = pixmap.fade_tables[256 * fade_level + block[(srcy << 8) + srcx]];
                ++d;
              }
              dbuf += dst_stride;
              ylim++;
          }
        } else
        {
          for (py = scaled_height; py > 0; py--)
          {
            xlim = wlimits;
            d = dbuf;
            srcy = (((*ylim) & 0xFF0000u) >> 16);
            for (px = scaled_width; px > 0; px--)
            {
              srcx = (((*xlim) & 0xFF0000u) >> 16);
              xlim++;
              *d = block[(srcy << 8) + srcx];
              ++d;
            }
            dbuf += dst_stride;
            ylim++;
          }
        }
    } else
    {
        ylim = wlimits;
        for (i = scaled_height; i > 0; i--)
        {
          *ylim = ystart;
          ylim++;
          ystart += yend;
        }
        xlim = hlimits;
        for (i = scaled_width; i > 0; i--)
        {
          *xlim = xstart;
          xlim++;
          xstart += xend;
        }
        dbuf = &vec_screen[local_screen_x + local_screen_y * dst_stride];
        block = block_ptrs[texture_block_index];
        ylim = wlimits;
        int32_t px;
        int32_t py;
        int32_t srcx;
        int32_t srcy;
        unsigned char *d;
        if ( fade_level >= 0 )
        {
          for (py = scaled_height; py > 0; py--)
          {
              xlim = hlimits;
              d = dbuf;
              srcy = (((*ylim) & 0xFF0000u) >> 16);
              for (px = scaled_width; px > 0; px--)
              {
                srcx = (((*xlim) & 0xFF0000u) >> 16);
                xlim++;
                *d = pixmap.fade_tables[256 * fade_level + block[(srcx << 8) + srcy]];
                ++d;
              }
              dbuf += dst_stride;
              ylim++;
          }
        } else
        {
          for (py = scaled_height; py > 0; py--)
          {
            xlim = hlimits;
            d = dbuf;
            srcy = (((*ylim) & 0xFF0000u) >> 16);
            for (px = scaled_width; px > 0; px--)
            {
              srcx = (((*xlim) & 0xFF0000u) >> 16);
              xlim++;
              *d = block[(srcx << 8) + srcy];
              ++d;
            }
            dbuf += dst_stride;
            ylim++;
          }
        }
    }
}

void draw_texture(int32_t texture_x, int32_t texture_y, int32_t texture_width, int32_t texture_height, int32_t texture_block_index, int32_t flags, int32_t fade_level)
{
    scale_tmap2(texture_block_index, flags, fade_level,
                texture_x / pixel_size, texture_y / pixel_size,
                texture_width / pixel_size, texture_height / pixel_size);
}

/******************************************************************************/
