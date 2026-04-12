/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sprfnt.h
 *     Header file for bflib_sprfnt.c.
 * @par Purpose:
 *     Bitmap sprite fonts support library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     29 Dec 2008 - 11 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_SPRFNT_H
#define BFLIB_SPRFNT_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_DRAW_MAX_LEN 4096

enum TbFontDrawFlags {
  Fnt_LeftJustify   = 0x00,
  Fnt_RightJustify  = 0x01,
  Fnt_CenterPos     = 0x02,
  Fnt_CenterLeftPos = 0x03,
  };

/******************************************************************************/
#pragma pack(1)

struct TbSprite;
struct TbSetupSprite;

struct AsianFont {
  const char *fname;
  unsigned char *data;
  unsigned long data_length;
  unsigned long chars_count;
  unsigned long ndata_shift;
  unsigned long ndata_scanline;
  unsigned long sdata_shift;
  unsigned long sdata_scanline;
  unsigned long narrow_width;
  unsigned long narrow_height;
  unsigned long bits_width;
  unsigned long bits_height;
  unsigned long narrow_spacing;
  unsigned long kana_spacing;
  unsigned long wide_spacing;
  unsigned long baseline_offset;
  unsigned long line_spacing;
};

struct AsianDraw {
  unsigned long draw_char;
  unsigned long bits_width;
  unsigned long bits_height;
  unsigned long character_spacing;
  unsigned long vertical_offset;
  unsigned long y_spacing;
  unsigned char *sprite_data;
};

/**
 * Defines a font drawing window.
 * Values are signed to ease comparison with negative values.
 */
struct AsianFontWindow {
  long width;
  long height;
  long scanline;
  unsigned char *buf_ptr;
};

extern short dbc_language;
extern TbBool dbc_enabled;
extern TbBool dbc_initialized;
extern const struct TbSpriteSheet *lbFontPtr;
extern struct AsianFont *active_dbcfont;
extern long dbc_colour0;
extern long dbc_colour1;

/******************************************************************************/


#pragma pack()
/******************************************************************************/
TbBool LbTextDraw(int posx, int posy, const char *text);
#define LbTextDrawFmt(posx, posy, fmt, ...) LbTextDrawResizedFmt(posx, posy, 16, fmt, ##__VA_ARGS__)
TbBool LbTextDrawResized(int posx, int posy, int units_per_px, const char *text);
TbBool LbTextDrawResizedFmt(int posx, int posy, int units_per_px, const char *fmt, ...);
int LbTextHeight(const char *text);
int LbTextLineHeight(void);
int LbTextSetWindow(int posx, int posy, int width, int height);
void LbTextGetJustifyWindowOrigin(int *x, int *y);
void LbTextGetJustifyWindow(int *x, int *y, int *width);
void LbTextGetClipWindow(int *x, int *y, int *width, int *height);
int  LbTextGetSpacesPerTab(void);
TbResult LbTextSetJustifyWindow(int pos_x, int pos_y, int width);
TbResult LbTextSetClipWindow(int x1, int y1, int x2, int y2);
TbBool LbTextSetFont(const struct TbSpriteSheet *font);
unsigned char LbTextGetFontFaceColor(void);
unsigned char LbTextGetFontBackColor(void);
int LbTextStringWidth(const char *str);
int LbTextStringPartWidth(const char *text, int part);
int LbTextStringHeight(const char *str);
int LbTextWordWidth(const char *str);
int LbTextCharWidth(const long chr);
int LbTextCharWidthM(const long chr, long units_per_px);
int LbTextStringWidthM(const char *str, long units_per_px);
int LbTextWordWidthM(const char *str, long units_per_px);

int LbTextNumberDraw(int pos_x, int pos_y, int units_per_px, long number, unsigned short fdflags);
int LbTextStringDraw(int pos_x, int pos_y, int units_per_px, const char *text, unsigned short fdflags);

// Function which require font sprites as parameter
int LbSprFontWordWidth(const struct TbSpriteSheet * font, const char * text);
int LbSprFontCharWidth(const struct TbSpriteSheet * font, const unsigned long chr);
int LbSprFontCharHeight(const struct TbSpriteSheet * font,const unsigned long chr);
const struct TbSprite * LbFontCharSprite(const struct TbSpriteSheet * font, const unsigned long chr);

void LbTextUseByteCoding(TbBool is_enabled);
long text_string_height(int units_per_px, const char *text);
int LbTextStringPartWidthM(const char *text, int part, long units_per_px);
void LbDrawCharUnderline(long pos_x, long pos_y, long width, long height,
    unsigned char draw_colr, unsigned char shadow_colr);
int get_bit_to_array(unsigned char *arrD, int iX, int iY, int iMax);
void set_bit_to_array(unsigned char *arrD, int iX, int iY, int iMax, int iValue);

// DBC (Double Byte Coding) font support
void dbc_set_language(short ilng);
short dbc_initialize(const char *fpath);
TbBool is_wide_charcode(unsigned long chr);
unsigned short dbc_char_to_font_char(unsigned long chr);
long dbc_char_width(unsigned long chr);
long dbc_char_widthM(unsigned long chr, long units_per_px);
long dbc_char_height(unsigned long chr);
int  dbc_get_sprite_for_char(struct AsianDraw *adraw, unsigned long chr);
int  dbc_draw_font_sprite_text(const struct AsianFontWindow *awind,
    const struct AsianDraw *adraw, long pos_x, long pos_y,
    short colr1, short colr2, short colr3);
TbBool change_dbcfont(int nfont);
int  dbc_fonts_count(void);
struct AsianFont *dbc_fonts_list(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
