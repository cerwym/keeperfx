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

/******************************************************************************/


#pragma pack()
/******************************************************************************/
TbBool LbTextDraw(int posx, int posy, const char *text);
#define LbTextDrawFmt(posx, posy, fmt, ...) LbTextDrawResizedFmt(posx, posy, 16, fmt, ##__VA_ARGS__)
TbBool LbTextDrawResized(int posx, int posy, int units_per_px, const char *text);
TbBool LbTextDrawResizedFmt(int posx, int posy, int units_per_px, const char *fmt, ...);
/* Software text renderer implementation — do not call directly from game code */
TbBool LbTextDraw_sw(int posx, int posy, const char *text);
TbBool LbTextDrawResized_sw(int posx, int posy, int units_per_px, const char *text);
TbBool LbTextDrawResizedFmt_sw(int posx, int posy, int units_per_px, const char *fmt, ...);
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

// Sub-routines, used for drawing text strings. For use in custom drawing methods.
TbBool LbAlignMethodSet(unsigned short fdflags);
long LbGetJustifiedCharPosX(long startx, long all_chars_width, long spr_width, long mul_width, unsigned short fdflags);
long LbGetJustifiedCharPosY(long starty, long all_lines_height, long spr_height, unsigned short fdflags);
long LbGetJustifiedCharWidth(long all_chars_width, long spr_width, long words_count, int units_per_px, unsigned short fdflags);

/** Callback invoked by LbTextLayout once per justified line segment.
 *  @param sbuf      Start of text segment (inclusive)
 *  @param ebuf      End of text segment (exclusive)
 *  @param x         X position in clip-window-relative coordinates
 *  @param y         Y position in clip-window-relative coordinates
 *  @param space_len Pixel width of a space for this segment (may be justify-expanded)
 *  @param units_per_px Scale factor (16 = 100%)
 *  @param userdata  Caller-supplied context pointer */
typedef void (*LbTextSegmentFn)(const char* sbuf, const char* ebuf,
                                long x, long y, long space_len,
                                int units_per_px, void* userdata);

/** Shared paragraph layout engine.
 *  Walks the text string applying word-wrap and justification against
 *  lbTextJustifyWindow, calling draw_fn once for each justified line segment.
 *  Coordinates passed to draw_fn are clip-window-relative (i.e. relative to
 *  the origin of lbTextClipWindow).
 *  Caller must set lbFontPtr, lbDisplay.DrawFlags, lbTextJustifyWindow and
 *  lbTextClipWindow before calling. */
void LbTextLayout(int posx, int posy, int units_per_px, const char* text,
                  LbTextSegmentFn draw_fn, void* userdata);

// Function which require font sprites as parameter
int LbSprFontWordWidth(const struct TbSpriteSheet * font, const char * text);
int LbSprFontCharWidth(const struct TbSpriteSheet * font, const unsigned long chr);
int LbSprFontCharHeight(const struct TbSpriteSheet * font,const unsigned long chr);
const struct TbSprite * LbFontCharSprite(const struct TbSpriteSheet * font, const unsigned long chr);

void LbTextUseByteCoding(TbBool is_enabled);
long text_string_height(int units_per_px, const char *text);
void dbc_set_language(short ilng);
short dbc_initialize(const char *fpath);
TbBool is_wide_charcode(unsigned long chr);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
