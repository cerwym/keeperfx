/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw.h
 *     Header file for bflib_vidraw.c.
 * @par Purpose:
 *     Graphics canvas drawing library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 10 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_VIDRAW_H
#define BFLIB_VIDRAW_H

#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define MAX_SUPPORTED_SPRITE_DIM 256

#define NUM_DRAWITEMS 238
#define SPRITE_SCALING_XSTEPS max(MAX_SUPPORTED_SPRITE_DIM,MAX_SUPPORTED_SCREEN_WIDTH)
#define SPRITE_SCALING_YSTEPS max(MAX_SUPPORTED_SPRITE_DIM,MAX_SUPPORTED_SCREEN_HEIGHT)

/** Set to 1 while a renderer submit is in-flight to prevent recursive re-entry into submit wrappers. */
extern int lb_in_sprite_submit;
/******************************************************************************/
#pragma pack(1)

struct TiledSprite;
struct TbSprite;
struct TbHugeSprite;

typedef void FlicFunc(void);

struct StartScreenPoint {
        short X;
        short Y;
};

//Note: this name is incorrect! (not from game)
struct LongPoint {
        long X;
        long Y;
};

struct EnginePoint {
        long X;
        long Y;
        long TMapX;
        long TMapY;
        long Shade;
        long coordinate_x_3d;
        long coordinate_y_3d;
        long coordinate_z_3d;
        long DistSqr;
        unsigned short padw;
        unsigned char Flags;
        unsigned char padb;
};

struct TbDItmHotspot {
        short X;
        short Y;
};

struct TbDItmFlic {
        FlicFunc *Function;
        TbPixel Colour;
};

struct TbDItmText {
        short WindowX;
        short WindowY;
        short Width;
        short Height;
        short X;
        short Y;
        const char *Text;
        struct TbSprite *Font;
        unsigned short Line;
        TbPixel Colour;
};

struct TbDItmSprite {
        short X;
        short Y;
        struct TbSprite *Sprite;
        TbPixel Colour;
};

struct TbDItmTrig {
        short vertex_2_x;
        short vertex_2_y;
        short vertex_3_x;
        short vertex_3_y;
        TbPixel Colour;
};

struct TbDItmTriangle {
        short vertex_1_x;
        short vertex_1_y;
        short vertex_2_x;
        short vertex_2_y;
        short vertex_3_x;
        short vertex_3_y;
        TbPixel Colour;
};

struct TbDItmBox {
        short X;
        short Y;
        short Width;
        short Height;
        TbPixel Colour;
};

struct TbDItmLine {
        short vertex_1_x;
        short vertex_1_y;
        short vertex_2_x;
        short vertex_2_y;
        TbPixel Colour;
};

union TbDItmU {
        struct TbDItmTrig Trig;
        struct TbDItmTriangle Triangle;
        struct TbDItmBox Box;
        struct TbDItmLine Line;
        struct TbDItmSprite Sprite;
        struct TbDItmText Text;
        struct TbDItmFlic Flic;
        struct TbDItmHotspot Hotspot;
};

//Original size (incl. any padding) = 26 bytes
struct PurpleDrawItem {
        union TbDItmU U;
        // pos=23d
        unsigned char Type;
        // pos=24d
        unsigned short Flags;
};

struct TbSourceBuffer {
        const void * data;
        unsigned long width;
        unsigned long height;
        unsigned long pitch;
};

/******************************************************************************/
extern unsigned char *poly_screen;
extern unsigned char *vec_screen;
extern unsigned char *vec_map;
extern unsigned long vec_screen_width;
extern long vec_window_width;
extern long vec_window_height;
extern unsigned char *dither_map;
extern unsigned char *dither_end;
extern unsigned char *lbSpriteReMapPtr;
extern long scale_up;
/** Per-call draw flags for the software rasteriser.  Set by every Lb*
 *  entry point from either a caller-supplied parameter or lbDisplay.DrawFlags.
 *  Internal rasteriser routines read this instead of the global. */
extern TbDrawFlagsMask lb_draw_flags;
extern int32_t xsteps_array[2*SPRITE_SCALING_XSTEPS];
extern int32_t ysteps_array[2*SPRITE_SCALING_YSTEPS];

#pragma pack()

/******************************************************************************/
void LbDrawBoxClip(long x, long y, unsigned long width, unsigned long height, TbPixel colour, TbDrawFlagsMask draw_flags);
TbResult LbDrawBox(long x, long y, unsigned long width, unsigned long height, TbPixel colour, TbDrawFlagsMask draw_flags);
void LbDrawHVLine(long xpos1, long ypos1, long xpos2, long ypos2, TbPixel colour);

void LbDrawPixel(long x, long y, TbPixel colour);
void LbDrawCircle(long x, long y, long radius, TbPixel colour, TbDrawFlagsMask draw_flags);

void setup_vecs(unsigned char *screenbuf, unsigned char *nvec_map,
        unsigned int line_len, unsigned int width, unsigned int height);
void setup_steps(long posx, long posy, const struct TbSourceBuffer * src_buf, int32_t **xstep, int32_t **ystep, int *scanline);
void setup_outbuf(const int32_t *xstep, const int32_t *ystep, uchar **outbuf, int *outheight);
TbResult LbSpriteDrawUsingScalingData(long posx, long posy, const struct TbSourceBuffer *, TbDrawFlagsMask draw_flags);
TbResult LbSpriteDrawRemapUsingScalingData(long posx, long posy, const struct TbSourceBuffer *, const TbPixel *cmap, TbDrawFlagsMask draw_flags);
TbResult LbSpriteDrawOneColourUsingScalingData(long posx, long posy, const struct TbSprite *sprite, TbPixel colour, TbDrawFlagsMask draw_flags);
void LbSpriteSetScalingData(long x, long y, long swidth, long sheight, long dwidth, long dheight);
/** Last parameters saved by LbSpriteSetScalingData \u2014 read by the GPU keeper-sprite hook. */
extern long g_sprite_scale_dst_x;
extern long g_sprite_scale_dst_y;
extern long g_sprite_scale_dst_w;
extern long g_sprite_scale_dst_h;
extern long g_sprite_scale_src_w;
extern long g_sprite_scale_src_h;
TbResult DrawAlphaSpriteUsingScalingData(long posx, long posy, const struct TbSourceBuffer *, TbDrawFlagsMask draw_flags);
void LbSpriteSetScalingWidthSimpleArray(int32_t * xsteps_arr, long x, long swidth, long dwidth);
void LbSpriteSetScalingWidthClippedArray(int32_t * xsteps_arr, long x, long swidth, long dwidth, long gwidth);
void LbSpriteSetScalingHeightSimpleArray(int32_t * ysteps_arr, long y, long sheight, long dheight);
void LbSpriteSetScalingHeightClippedArray(int32_t * ysteps_arr, long y, long sheight, long dheight, long gheight);

TbResult LbSpriteDraw(long x, long y, const struct TbSprite *spr, TbDrawFlagsMask draw_flags);
TbResult LbSpriteDrawOneColour(long x, long y, const struct TbSprite *spr, const TbPixel colour, TbDrawFlagsMask draw_flags);

TbResult LbSpriteDrawScaled(long xpos, long ypos, const struct TbSprite *sprite, long dest_width, long dest_height, TbDrawFlagsMask draw_flags);
TbResult LbSpriteDrawScaledOneColour(long xpos, long ypos, const struct TbSprite *sprite, long dest_width, long dest_height, const TbPixel colour, TbDrawFlagsMask draw_flags);
int LbSpriteDrawScaledRemap(long xpos, long ypos, const struct TbSprite *sprite, long dest_width, long dest_height, const unsigned char *cmap, TbDrawFlagsMask draw_flags);
#define LbSpriteDrawResized(xpos, ypos, un_per_px, sprite, draw_flags) LbSpriteDrawScaled(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, draw_flags)
#define LbSpriteDrawResizedOneColour(xpos, ypos, un_per_px, sprite, colour, draw_flags) LbSpriteDrawScaledOneColour(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, colour, draw_flags)
#define LbSpriteDrawResizedRemap(xpos, ypos, un_per_px, sprite, cmap, draw_flags) LbSpriteDrawScaledRemap(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, cmap, draw_flags)

TbResult LbHugeSpriteDraw(const struct TbHugeSprite * spr, long sp_len,
    unsigned char *r, int r_row_delta, int r_height, short xshift, short yshift, int units_per_px);
void LbTiledSpriteDraw(long x, long y, long units_per_px, struct TiledSprite *bigspr, TbDrawFlagsMask draw_flags);
int LbTiledSpriteHeight(struct TiledSprite *bigspr);

// mspointer needs this for some reason
TbResult LbSpriteDrawUsingScalingUpDataSolidLR(uchar *outbuf, int scanline, int outheight, int32_t *xstep, int32_t *ystep, const struct TbSourceBuffer * src_buf);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
