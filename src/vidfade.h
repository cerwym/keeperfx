/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidfade.h
 *     Header file for vidfade.c.
 * @par Purpose:
 *     Video fading routines.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     16 Jul 2010 - 05 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_VIDFADE_H
#define DK_VIDFADE_H

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COLOUR_TABLE_BITS_PER_VALUE 4
#define COLOUR_TABLE_DIMENSION (1<<COLOUR_TABLE_BITS_PER_VALUE)

/******************************************************************************/
#pragma pack(1)

struct Thing;
struct PlayerInfo;
typedef unsigned char TbRGBColorTable[COLOUR_TABLE_DIMENSION][COLOUR_TABLE_DIMENSION][COLOUR_TABLE_DIMENSION];

struct TbColorTables {
  unsigned char fade_tables[64*256];
  unsigned char ghost[256*256];
  unsigned char flat_colours_tl[2*256];
  unsigned char flat_colours_tr[2*256];
  unsigned char flat_colours_br[2*256];
  unsigned char flat_colours_bl[2*256];
  unsigned char robs_bollocks[256];
};

struct TbAlphaTables {
    unsigned char void_black[256];
    unsigned char white[8*256];
    unsigned char yellow[8*256];
    unsigned char red[8*256];
    unsigned char blue[8*256];
    unsigned char green[8*256];
    unsigned char purple[8*256];
    unsigned char black[8*256];
    unsigned char orange[8*256];
    // This is to force the array to have 256x256 size
    //unsigned char unused[191*256];
};

/******************************************************************************/
extern unsigned char fade_palette_in;
extern unsigned char frontend_palette[768];
extern TbRGBColorTable colours;
extern float g_palette_possession_tint;

extern struct TbColorTables pixmap;
extern struct TbAlphaTables alpha_sprite_table;
extern unsigned char white_pal[256];
extern unsigned char red_pal[256];

#pragma pack()
/******************************************************************************/
void fade_in(void);
void fade_out(void);
void compute_fade_tables(struct TbColorTables *coltbl,unsigned char *spal,unsigned char *dpal);
void ProperFadePalette(unsigned char *pal, long fade_steps, enum TbPaletteFadeFlag flg);
void ProperForcedFadePalette(unsigned char *pal, long n, enum TbPaletteFadeFlag flg);

void compute_alpha_tables(struct TbAlphaTables *alphtbls,unsigned char *spal,unsigned char *dpal);
void compute_rgb2idx_table(TbRGBColorTable ctab,unsigned char *spal);
void compute_shifted_palette_table(TbPixel *ocol, const unsigned char *spal,
    const unsigned char *dpal, int shiftR, int shiftG, int shiftB);


long PaletteFadePlayer(struct PlayerInfo *player);
void PaletteApplyPainToPlayer(struct PlayerInfo *player, long intense);

TbBool init_fades_table(void);
TbBool init_alpha_table(void);
void init_colours(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
