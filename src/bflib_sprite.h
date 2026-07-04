/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sprite.h
 *     Header file for bflib_sprite.c.
 * @par Purpose:
 *     Graphics sprites support library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 30 Dec 2008
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_SPRITE_H
#define BFLIB_SPRITE_H

#include "globals.h"
#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

/**
 * Type which contains buffer of a sprite, with RLE-encoded alpha channel.
 */
typedef unsigned char * TbSpriteData;

struct TbSprite {
    TbSpriteData Data;
    unsigned char SWidth;
    unsigned char SHeight;
};

struct TbSpriteSheet;

struct TbSetupSprite {
    struct TbSprite **Start;
    struct TbSprite **End;
    TbSpriteData *Data;
};

struct TbHugeSprite {
    TbSpriteData Data;  //**< Raw sprite data, with RLE coded transparency.
    int32_t * Lines;  //**< Index of line starts in the sprite data.
    unsigned long SWidth;
    unsigned long SHeight;
};

struct TiledSprite {
    unsigned char x_num;
    unsigned char y_num;
    unsigned short spr_idx[10][10];
};
#pragma pack()

struct TbSpriteSheet * create_spritesheet(void);
struct TbSpriteSheet * load_spritesheet(const char * data_fname, const char * index_fname);
void free_spritesheet(struct TbSpriteSheet **);
const struct TbSprite * get_sprite(const struct TbSpriteSheet *, long index);
TbBool add_sprite(struct TbSpriteSheet * sheet, unsigned char width, unsigned char height, int size, const void * data);
long num_sprites(const struct TbSpriteSheet *);
void trim_spritesheet(struct TbSpriteSheet *sheet, long count);

#define load_font(data_fname, index_fname) load_spritesheet(data_fname, index_fname)
#define free_font(font) free_spritesheet(font)

/******************************************************************************/
#ifdef __cplusplus
}
#endif

/**
 * Decode a TbSprite RLE stream into a flat palette-index buffer.
 *
 * Each output byte is a raw palette index; index 0 means "transparent" to
 * every GPU consumer of this buffer (sprite/font atlas shaders discard texel
 * value 0). Transparent (skipped) spans write nothing, leaving the
 * pre-zeroed destination as 0. But a *copied* (opaque) run can legitimately
 * contain literal index 0 (e.g. black) in the source art — those bytes are
 * remapped to 1 so they stay indistinguishable from "real" transparency.
 * This mirrors the equivalent 0->1 substitution already applied to the slab
 * tile texture in gui_draw.c (draw_slab64k).
 *
 * The format: a stream of signed bytes where
 *   0       = end of row (advance to next row),
 *   n < 0   = skip -n transparent pixels,
 *   n > 0   = copy the next n palette-index bytes verbatim.
 *
 * @param dst        Destination pixel buffer.
 * @param dst_stride Row pitch of @p dst in bytes (>= @p w).
 * @param data       Pointer to the RLE-encoded sprite data.
 * @param w          Sprite width in pixels.
 * @param h          Sprite height in pixels.
 */
static inline void LbSpriteDecode(unsigned char *dst, int dst_stride,
                                   const unsigned char *data, int w, int h)
{
    int y;
    for (y = 0; y < h; ++y)
        memset(dst + y * dst_stride, 0, (size_t)w);

    for (y = 0; y < h; ++y) {
        unsigned char *row = dst + y * dst_stride;
        int x = 0;
        for (;;) {
            signed char cmd = (signed char)(*data++);
            if (cmd == 0) break;
            if (cmd < 0) {
                x += (int)(-cmd);
            } else {
                int count = (int)cmd;
                int i;
                for (i = 0; i < count; ++i) {
                    if (x < w) row[x] = *data ? *data : 1;
                    ++data;
                    ++x;
                }
            }
        }
    }
}

#ifdef __cplusplus
/** C++ overload: unwrap a TbSprite directly. */
inline void LbSpriteDecode(unsigned char *dst, int dst_stride,
                            const struct TbSprite *spr)
{
    LbSpriteDecode(dst, dst_stride, spr->Data, spr->SWidth, spr->SHeight);
}
#endif

#endif
