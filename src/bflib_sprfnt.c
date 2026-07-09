/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sprfnt.c
 *     Bitmap sprite fonts support library.
 * @par Purpose:
 *     Functions for reading bitmap sprite fonts and using them to display text.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     29 Dec 2008 - 11 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_sprfnt.h"

#include <stdarg.h>

#include "bflib_sprite.h"
#include "bflib_fileio.h"
#include "bflib_vidraw.h"
#include "renderer/RendererManager.h"

//TODO: this breaks my convention - non-bflib call from bflib (used for asian fonts)
#include "frontend.h"
#include "front_credits.h"
#include "config_keeperfx.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define DOUBLE_UNDERLINE_BOUND 16

struct AsianFont dbcJapFonts[] = {
  {"font12j.fon", 0, 215136, 0x2284, 0, 12, 0x0C00, 24, 6, 12, 12, 12, 0, 1, 1, 1, 1},
  {"font16j.fon", 0, 286848, 0x2284, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
  {"font24j.fon", 0, 561744, 0x2284, 0, 24,      0, 72, 8, 24, 24, 24, 0, 1, 1, 4, 2},
};

struct AsianFont dbcChiFonts[] = {
  {"font12c.fon", 0, 199344, 0x1FF2, 0, 12, 0x0C00, 24, 6, 12, 12, 12, 0, 1, 1, 1, 1},
  {"font16c.fon", 0, 271712, 0x20AB, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
  {"font16c.fon", 0, 271712, 0x20AB, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
};

struct AsianFont dbcChtFonts[] = {
  {"font12f.fon", 0, 215700, 0x1FF2, 0, 12, 0x0C00, 26, 6, 12, 15, 13, 0, 1, 1, 1, 1},
  {"font16f.fon", 0, 265792, 0x1FF2, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
  {"font16f.fon", 0, 265792, 0x1FF2, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
};

struct AsianFont dbcKorFonts[] = {
  {"font16k.fon", 0, 271712, 0x20AB, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
  {"font16k.fon", 0, 271712, 0x20AB, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
  {"font16k.fon", 0, 271712, 0x20AB, 0, 16, 0x1000, 32, 8, 16, 16, 16, 0, 1, 1, 4, 2},
};

struct AsianFont *active_dbcfont = &dbcJapFonts[0];
short dbc_language = 0;
TbBool dbc_initialized = false;
TbBool dbc_enabled = true;

static unsigned char lbSpacesPerTab;
/******************************************************************************/

/** Returns if the given char starts a wide charcode.
 * @param chr
 */
TbBool is_wide_charcode(unsigned long chr)
{
  if (chr > 0xFF)
    return true;
  if ((dbc_initialized) && (dbc_enabled))
  {
    switch (dbc_language)
    {
    case DbcId_Japanese:
        return ((chr >= 0x81) && (chr <= 0x9F)) || ((chr >= 0xE0) && (chr <= 0xFC));
    case DbcId_ChineseInt:
    case DbcId_ChineseTra:
    case DbcId_Korean:
        return ((chr > 0x80) && (chr <= 0xFF));
    }
  }
  return false;
}

/**
 * Draws an underline below the character.
 * @param pos_x
 * @param pos_y
 * @param width
 * @param height
 * @param draw_colr
 * @param shadow_colr
 */
void LbDrawCharUnderline(long pos_x, long pos_y, long width, long height, uchar draw_colr, uchar shadow_colr, TbDrawFlagsMask draw_flags)
{
    long h = height;
    long w = width;
    // Draw shadow
    if ((draw_flags & Lb_TEXT_UNDERLNSHADOW) != 0) {
        long shadow_x = pos_x + 1;
        if (height > 2*DOUBLE_UNDERLINE_BOUND)
            shadow_x++;
        LbDrawHVLine(shadow_x, pos_y+h, shadow_x+w, pos_y+h, shadow_colr);
        h--;
        if (height > DOUBLE_UNDERLINE_BOUND) {
            LbDrawHVLine(shadow_x, pos_y+h, shadow_x+w, pos_y+h, shadow_colr);
            h--;
        }
    }
    // Draw underline
    LbDrawHVLine(pos_x, pos_y+h, pos_x+w, pos_y+h, draw_colr);
    h--;
    if (height > DOUBLE_UNDERLINE_BOUND) {
        LbDrawHVLine(pos_x, pos_y+h, pos_x+w, pos_y+h, draw_colr);
        h--;
    }
}

unsigned short dbc_char_to_font_char(unsigned long chr)
{
    unsigned char i;
    unsigned char k;
    unsigned short font_char;
    switch (dbc_language)
    {
    default:
    case DbcId_Japanese:
    {
        i = ((chr)&0xFF);
        if (i >= 128)
          i-=32;
        else
          i-=31;
        k = ((chr>>8)&0xFF) - 129;
        if (k >= 31)
          k -= 64;
        k *= 2;
        if (i >= 127)
        {
          k++;
          i -= 94;
        }
        unsigned short n = ((k + 33) << 8) + i - (33 << 8) - 33;
        font_char = 94 * ((n >> 8)&0xFF) + ((n)&0xFF);
        break;
    }
    case DbcId_ChineseInt:
    case DbcId_ChineseTra:
    case DbcId_Korean:
        i = ((chr)&0xFF);
        k = ((chr>>8)&0xFF);
        font_char = 94 * (short)k + i - 15295;
        break;
    }
    SYNCDBG(19,"Char %04X converted to %d",(int)chr,(int)font_char);
    return font_char;
}

int dbc_get_sprite_for_char(struct AsianDraw *adraw, unsigned long chr)
{
    long c;
    long i;
    SYNCDBG(19,"Starting");
    if (active_dbcfont->data == 0)
        return 5;
    if (adraw == NULL)
        return 4;
    if (chr >= 0xFF)
    {
        c = dbc_char_to_font_char(chr);
        if ((c < 0) || (c >= (long) active_dbcfont->chars_count))
          return 6;
        adraw->draw_char = chr;
        adraw->bits_width = active_dbcfont->bits_width;
        adraw->bits_height = active_dbcfont->bits_height;
        i = active_dbcfont->wide_spacing;
        adraw->character_spacing = i;
        adraw->vertical_offset = active_dbcfont->baseline_offset;
        adraw->y_spacing = active_dbcfont->line_spacing;
        i = c * active_dbcfont->sdata_scanline + active_dbcfont->sdata_shift;
        adraw->sprite_data = active_dbcfont->data + i;
        return 0;
    } else
    {
        adraw->draw_char = chr;
        c = chr;
        adraw->bits_width = active_dbcfont->narrow_width;
        adraw->bits_height = active_dbcfont->narrow_height;
        if ((c < 0xA0) || (c > 0xDF))
          i = active_dbcfont->narrow_spacing;
        else
          i = active_dbcfont->kana_spacing;
        adraw->character_spacing = i;
        adraw->vertical_offset = active_dbcfont->baseline_offset;
        adraw->y_spacing = active_dbcfont->line_spacing;
        i = c * active_dbcfont->ndata_scanline + active_dbcfont->ndata_shift;
        adraw->sprite_data = active_dbcfont->data + i;
        return 0;
    }
}

long dbc_char_height(unsigned long chr)
{
  if (is_wide_charcode(chr))
  {
    return active_dbcfont->line_spacing + active_dbcfont->baseline_offset + active_dbcfont->bits_height;
  } else
  {
    return active_dbcfont->line_spacing + active_dbcfont->baseline_offset + active_dbcfont->narrow_height;
  }
}

long dbc_char_width(unsigned long chr)
{
  if (chr == 0)
  {
    return 0;
  } else
  if (is_wide_charcode(chr))
  {
    return active_dbcfont->wide_spacing + active_dbcfont->bits_width;
  } else
  {
    return active_dbcfont->narrow_spacing + active_dbcfont->narrow_width;
  }
}

int dbc_draw_font_sprite(unsigned char *dst_buf, long dst_scanline, unsigned char *src_buf,
      unsigned short src_bitwidth, short start_x, short start_y, short width, short height,
      short colr1, short colr2)
{
    SYNCDBG(19,"Starting at %d,%d size %d,%d",(int)start_x, (int)start_y, (int)width, (int)height);
    // Computing width in bytes from the number of bits
    unsigned short src_scanline = src_bitwidth >> 3;
    if ((src_bitwidth & 7) != 0)
        src_scanline++;
    if (start_y != 0)
        src_buf += src_scanline * (long)start_y;
    unsigned short src_val = 0;
    for (int y = height; y > 0; y--)
    {
        unsigned char* src = src_buf;
        unsigned char* dst = dst_buf;
        short skip_count = start_x;
        for (int x = 0; x < start_x + width; x++)
        {
          if ((x & 7) == 0)
            src_val = *src++;
          src_val <<= 1;
          short colour;
          if ((src_val & 0x100) != 0)
              colour = colr1;
          else
            colour = colr2;
          if (skip_count > 0)
          {
            skip_count--;
            continue;
          }
          if ((colour & 0xFF00) == 0)
            *dst = colour;
          dst++;
        }
        src_buf += src_scanline;
        dst_buf += dst_scanline;
    }
    return 0;
}

int get_bit_to_array(unsigned char* arrD, int iX, int iY, int iMax)
{
    int iPos = (iY * iMax + iX);
    int iBytePos = iPos / 8;
    int iModBitPos = iPos % 8;
    return (*(arrD + iBytePos) & (0x80 >> iModBitPos)) == (0x80 >> iModBitPos) ? 1 : 0;
}

void set_bit_to_array(unsigned char* arrD, int iX, int iY, int iMax, int iValue)
{
    int iPos = (iY * iMax + iX);
    int iBytePos = iPos / 8;
    int iModBitPos = iPos % 8;
    if (iValue == 1)
        *(arrD + iBytePos) |= 0x80 >> iModBitPos;
    else
        *(arrD + iBytePos) &= ~(0x80 >> iModBitPos);
}

int dbc_fonts_count(void)
{
  switch (dbc_language)
  {
  case DbcId_Japanese:
       return (sizeof(dbcJapFonts)/sizeof(dbcJapFonts[0]));
  case DbcId_ChineseInt:
       return (sizeof(dbcChiFonts)/sizeof(dbcChiFonts[0]));
  case DbcId_ChineseTra:
       return (sizeof(dbcChtFonts)/sizeof(dbcChtFonts[0]));
  case DbcId_Korean:
      return (sizeof(dbcKorFonts) / sizeof(dbcKorFonts[0]));
  }
  return 0;
}

struct AsianFont *dbc_fonts_list(void)
{
  switch (dbc_language)
  {
  case DbcId_Japanese:
       return dbcJapFonts;
  case DbcId_ChineseInt:
       return dbcChiFonts;
  case DbcId_ChineseTra:
       return dbcChtFonts;
  case DbcId_Korean:
      return dbcKorFonts;
  }
  return NULL;
}

int dbc_draw_font_sprite_text(const struct AsianFontWindow *awind, const struct AsianDraw *adraw,
      long pos_x, long pos_y, short colr1, short colr2, short colr3)
{
    long scr_x;
    long scr_y;
    unsigned char *dst_buf;
    long width;
    long height;
    long x;
    long y;
    SYNCDBG(19,"Starting");
    if ((adraw == NULL) || (awind == NULL))
      return 4;
    if ((adraw->sprite_data == NULL) || (awind->buf_ptr == NULL))
      return 4;
    if (colr3 >= 0)
    {
      x = 0;
      y = 0;
      scr_y = adraw->vertical_offset + pos_y + 1;
      scr_x = pos_x + 1;
      width = adraw->bits_width;
      height = adraw->bits_height;
      if (scr_x < 0)
      {
        width += scr_x;
        if (width <= 0)
          goto skip_sprite_draw;
        x = -scr_x;
        scr_x = 0;
      } else
      if ((long) (scr_x + adraw->bits_width) > awind->width)
      {
        if (scr_x >= awind->width)
          goto skip_sprite_draw;
        width = awind->width - scr_x;
      }
      if (scr_y < 0)
      {
        height += scr_y;
        if (height > 0)
        {
          y = -scr_y;
          scr_y = 0;
          if ((width != 0) && (height != 0))
          {
            dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
            dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr3, -1);
          }
        }
      } else
      if (height+scr_y > awind->height)
      {
        if (scr_y < awind->height)
        {
          height = awind->height - scr_y;
          if ((width != 0) && (height != 0))
          {
            dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
            dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr3, -1);
          }
        }
      }
    }
skip_sprite_draw:
    if ((colr1 >= 0) || (colr2 >= 0))
    {
      y = 0;
      x = 0;
      width = adraw->bits_width;
      height = adraw->bits_height;
      scr_y = pos_y + adraw->vertical_offset;
      scr_x = pos_x;
      if (pos_x >= 0)
      {
        if (width + pos_x > awind->width)
        {
          if (pos_x >= awind->width)
            return 0;
          width = awind->width - pos_x;
        }
      } else
      {
        width += pos_x;
        if (width <= 0)
          return 0;
        scr_x = 0;
        x = -pos_x;
      }
      if (scr_y >= 0)
      {
        if (height + scr_y > awind->height)
        {
          if (scr_y >= awind->height)
            return 0;
          height = awind->height - scr_y;
        }
      } else
      {
        height += scr_y;
        if (height <= 0)
          return 0;
        y = -scr_y;
        scr_y = 0;
      }
      if ((width != 0) && (height != 0))
      {
        dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
        dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr1, colr2);
      }
    }
    return 0;
}


TbBool LbTextDraw(int posx, int posy, const char *text, TbDrawFlagsMask draw_flags)
{
    return TextRenderer_DrawTextResized(posx, posy, 16, text, draw_flags);
}

TbBool LbTextDrawResized(int posx, int posy, int units_per_px, const char *text, TbDrawFlagsMask draw_flags)
{
    return TextRenderer_DrawTextResized(posx, posy, units_per_px, text, draw_flags);
}

TbBool LbTextDrawResizedFmt(int posx, int posy, int units_per_px, TbDrawFlagsMask draw_flags, const char *fmt, ...)
{
    char * text = (char *)KfxAlloc(8192);
    if (text == NULL) return false;
    va_list val;
    va_start(val, fmt);
    vsnprintf(text, TEXT_DRAW_MAX_LEN, fmt, val);
    va_end(val);
    TbBool result = TextRenderer_DrawTextResized(posx, posy, units_per_px, text, draw_flags);
    KfxFree(text);
    return result;
}

/******************************************************************************/
/** Returns standard height of a line of text, in currently active font.
 *  Supports both sprite fonts and dbc fonts.
 */
int LbTextLineHeight(void) {
    return TextRenderer_LineHeight();
}

int LbTextHeight(const char *text)
{
    return TextRenderer_TextHeight(text);
}

long dbc_char_widthM(unsigned long chr, long units_per_px)
{
    if (chr == 0)
    {
        return 0;
    }

    long h = (units_per_px / 8) * 8;
    long w = h;
    if (!is_wide_charcode(chr))
    {
        w -= (8 * (w / 16));
    }
    else
    {
        // The old code has an additional 1. why?
        // w++;
    }

    struct AsianDraw adraw = { 0 };
    if (dbc_get_sprite_for_char(&adraw, chr) == 0)
    {
        w += adraw.character_spacing;
    }
    if (h == 16)
        w = w * units_per_px / 16;

    return w;
}

int LbTextCharWidthM(const long chr, long units_per_px)
{
    return TextRenderer_CharWidthScaled((uint32_t)chr, (int32_t)units_per_px);
}

int LbTextCharWidth(const long chr)
{
    return TextRenderer_CharWidth((uint32_t)chr);
}

int LbTextWordWidth(const char *str)
{
    return TextRenderer_WordWidth(str);
}


void LbTextUseByteCoding(TbBool is_enabled)
{
    dbc_enabled = is_enabled;
}

int LbTextSetWindow(int posx, int posy, int width, int height)
{
    TextRenderer_SetWindow(posx, posy, width, height);
    return 1;
}

void LbTextGetJustifyWindowOrigin(int *x, int *y)
{
    int32_t jx = 0, jy = 0;
    TextRenderer_GetJustifyWindow(&jx, &jy, NULL);
    if (x) *x = jx;
    if (y) *y = jy;
}

void LbTextGetJustifyWindow(int *x, int *y, int *width)
{
    int32_t jx = 0, jy = 0, jw = 0;
    TextRenderer_GetJustifyWindow(&jx, &jy, &jw);
    if (x)     *x     = jx;
    if (y)     *y     = jy;
    if (width) *width = jw;
}

void LbTextGetClipWindow(int *x, int *y, int *width, int *height)
{
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    TextRenderer_GetClipWindow(&cx, &cy, &cw, &ch);
    if (x)      *x      = cx;
    if (y)      *y      = cy;
    if (width)  *width  = cw;
    if (height) *height = ch;
}

int LbTextGetSpacesPerTab(void)
{
    return (int)lbSpacesPerTab;
}

TbBool change_dbcfont(int nfont)
{
    const long fonts_count = dbc_fonts_count();
    struct AsianFont *dbcfonts = dbc_fonts_list();
    if ((nfont >= 0) && (nfont < fonts_count) && (dbcfonts != NULL))
    {
        active_dbcfont = &dbcfonts[nfont];
        return true;
    }
    return false;
}

TbBool LbTextSetFont(const struct TbSpriteSheet *font)
{
    TextRenderer_SetFont(font);
    return true;
}

unsigned char LbTextGetFontFaceColor(const struct TbSpriteSheet *font)
{
    if (font == frontend_font[0]) {
      return 238;
    } else if (font == frontend_font[1]) {
      return 243;
    } else if (font == frontend_font[2]) {
      return 248;
    } else if (font == frontend_font[3]) {
      return 119;
    } else if (font == winfont) {
      return 73;
    } else if (font == font_sprites) {
      return 1;
    } else if (font == frontstory_font) {
      return 237;
    } else {
      return 70;
    }
}

unsigned char LbTextGetFontBackColor(const struct TbSpriteSheet *font)
{
    if (font == font_sprites) {
      return 0;
    } else if (font == frontstory_font) {
        return 232;
    } else {
        return 1;
    }
}

/**
 * Returns length of part of a text if drawn on screen.
 * @param text The text to be probed.
 * @param part Amount of characters to be probed.
 * @return Width of the text image, in pixels.
 */
int LbTextStringPartWidth(const char *text, int part)
{
    if (TextRenderer_GetFont() == NULL)
        return 0;
    int max_len = 0;
    int len = 0;
    for (const char* ebuf = text; *ebuf != '\0'; ebuf++)
    {
        if (part <= 0) break;
        part--;
        long chr = (unsigned char)*ebuf;
        if (is_wide_charcode(chr))
        {
          ebuf++;
          if (*ebuf == '\0') break;
          chr = (chr<<8) + (unsigned char)*ebuf;
        }
        if (chr > 31)
        {
          len += LbTextCharWidth(chr);
        } else
        if (chr == '\r')
        {
          if (len > max_len)
          {
            max_len = len;
          }
          len = 0;
        } else
        if (chr == '\t')
        {
          len += lbSpacesPerTab*LbTextCharWidth(' ');
        } else
        if ((chr == 6) || (chr == 7) || (chr == 8) || (chr == 9) || (chr == 14))
        {
          ebuf++;
          if (*ebuf == '\0')
            break;
        }
    }
    if (len > max_len)
        max_len = len;
    return max_len;
}

int LbTextStringPartWidthM(const char *text, int part, long units_per_px)
{
    if (TextRenderer_GetFont() == NULL)
        return 0;
    int max_len = 0;
    int len = 0;
    for (const char* ebuf = text; *ebuf != '\0'; ebuf++)
    {
        if (part <= 0) break;
        part--;
        long chr = (unsigned char)*ebuf;
        if (is_wide_charcode(chr))
        {
            ebuf++;
            if (*ebuf == '\0') break;
            chr = (chr << 8) + (unsigned char)*ebuf;
        }
        if (chr > 31)
        {
            len += LbTextCharWidthM(chr, units_per_px);
        }
        else
            if (chr == '\r')
            {
                if (len > max_len)
                {
                    max_len = len;
                    len = 0;
                }
            }
            else
                if (chr == '\t')
                {
                    len += lbSpacesPerTab * LbTextCharWidthM(' ', units_per_px);
                }
                else
                    if ((chr == 6) || (chr == 7) || (chr == 8) || (chr == 9) || (chr == 14))
                    {
                        ebuf++;
                        if (*ebuf == '\0')
                            break;
                    }
    }
    if (len > max_len)
        max_len = len;
    return max_len;
}

/**
 * Returns length of given text if drawn on screen.
 * @param text The text to be probed.
 * @return Width of the text image, in pixels.
 */
int LbTextStringWidth(const char *text)
{
    return TextRenderer_StringWidth(text);
}

int LbTextStringWidthM(const char *text, long units_per_px)
{
    return TextRenderer_StringWidthScaled(text, (int32_t)units_per_px);
}

/* @function
 *   Get the scaled length of word for multiple encodings, that is, compatible with dbc or non-dbc.
 *   Like LbTextCharWidthM, but change from one char to one word.
 *   One word defined as continuous and uninterrupted letters.
 *
 * @param units_per_px Scale in pixels.
 */
int LbTextWordWidthM(const char *str, long units_per_px)
{
    return TextRenderer_WordWidthScaled(str, (int32_t)units_per_px);
}

int LbTextStringHeight(const char *str)
{
    int lines = 1;
    if ((TextRenderer_GetFont() == NULL) || (str == NULL))
        return 0;
    for (int i = 0; i < MAX_TEXT_LENGTH; i++)
    {
        if (str[i]=='\0') break;
        if (str[i]=='\r') lines++;
    }
    int h = LbTextLineHeight();
    return h*lines;
}

long text_string_height(int units_per_px, const char *text)
{
    return (long)TextRenderer_StringHeight((int32_t)units_per_px, text);
}

int LbTextNumberDraw(int pos_x, int pos_y, int units_per_px, long number, unsigned short fdflags, TbDrawFlagsMask draw_flags)
{
    if (TextRenderer_GetFont() == NULL)
      return 0;
    char text[16] = "";
    snprintf(text, sizeof(text), "%ld", number);
    int h = LbTextLineHeight() * units_per_px / 16;
    int w = LbTextStringWidthM(text, units_per_px);
    switch (fdflags & 0x03)
    {
    case Fnt_LeftJustify:
        LbTextSetWindow(pos_x, pos_y, w, h);
        break;
    case Fnt_RightJustify:
        LbTextSetWindow(pos_x-w, pos_y, w, h);
        break;
    case Fnt_CenterPos:
        LbTextSetWindow(pos_x-(w>>1), pos_y, w, h);
        break;
    }
    LbTextDrawResized(0, 0, units_per_px, text, draw_flags);
    return w;
}

int LbTextStringDraw(int pos_x, int pos_y, int units_per_px, const char *text, unsigned short fdflags, TbDrawFlagsMask draw_flags)
{
    if (TextRenderer_GetFont() == NULL)
      return 0;
    if (text == NULL)
      return 0;
    int h = LbTextLineHeight() * units_per_px / 16;
    int w = LbTextStringWidthM(text, units_per_px);
    switch (fdflags & 0x03)
    {
    case Fnt_LeftJustify:
        LbTextSetWindow(pos_x, pos_y, w, h);
        break;
    case Fnt_RightJustify:
        LbTextSetWindow(pos_x-w, pos_y, w, h);
        break;
    case Fnt_CenterPos:
        LbTextSetWindow(pos_x-(w>>1), pos_y, w, h);
        break;
    }
    LbTextDrawResized(0, 0, units_per_px, text, draw_flags);
    return w;
}

TbResult LbTextSetJustifyWindow(int pos_x, int pos_y, int width)
{
    TextRenderer_SetJustifyWindow(pos_x, pos_y, width);
    return Lb_SUCCESS;
}

TbResult LbTextSetClipWindow(int pos_x, int pos_y, int width, int height)
{
    TextRenderer_SetClipWindow(pos_x, pos_y, width, height);
    return Lb_SUCCESS;
}

/**
 * Computes width of one word in given string, starting at given pointer.
 * The word may end with NULL character, space, tab or line end / return carret.
 * @note Works only for characters stored in the sprite list.
 *       Multibyte characters are usually stored somewhere else.
 */
int LbSprFontWordWidth(const struct TbSpriteSheet * font, const char * text)
{
  if ((font == NULL) || (text == NULL))
    return 0;
  const char* c = text;
  int len = 0;
  while ((*c != ' ') && (*c != '\t') && (*c != '\0') && (*c != '\r') && (*c != '\n'))
  {
    if ((unsigned char)(*c) > 32)
      len += LbSprFontCharWidth(font,(unsigned char)*c);
    c++;
  }
  return len;
}

/**
 * Computes width of a single character in given font.
 * For characters that don't have a sprite (like tab), returns 0.
 * @note Works only for characters stored in the sprite list.
 *       Multibyte characters are usually stored somewhere else.
 */
int LbSprFontCharWidth(const struct TbSpriteSheet * font, const unsigned long chr)
{
    const struct TbSprite* spr = LbFontCharSprite(font, chr);
    if (spr == NULL)
        return 0;
    return spr->SWidth;
}

/**
 * Computes height of a single character in given font.
 * For characters that don't have a sprite (like tab), returns 0.
 * @note Works only for characters stored in the sprite list.
 *       Multibyte characters are usually stored somewhere else.
 */
int LbSprFontCharHeight(const struct TbSpriteSheet * font, const unsigned long chr)
{
    const struct TbSprite* spr = LbFontCharSprite(font, chr);
    if (spr == NULL)
        return 0;
    return spr->SHeight;
}

/**
 * Returns sprite of a single character in given font.
 * For characters that don't have a sprite, returns NULL.
 */
const struct TbSprite * LbFontCharSprite(const struct TbSpriteSheet * font, const unsigned long chr)
{
    if (font == NULL) {
        return NULL;
    } else if ((chr >= 31) && (chr < 256)) {
        return get_sprite(font, chr - 31);
    } else if ((chr > 14) && (chr < 31)) {
        return get_sprite(font,chr + 208); //223 was the biggest value that fits in the regular 255 slots, but since 15~30 was free still, we add those to the end.
    }
    return NULL;
}

void dbc_shutdown(void)
{
  const long fonts_count = dbc_fonts_count();
  struct AsianFont *dbcfonts = dbc_fonts_list();
  for (long i = 0; i < fonts_count; i++)
  {
    active_dbcfont = &dbcfonts[i];
    if (active_dbcfont->data != NULL)
    {
      KfxFree(active_dbcfont->data);
      active_dbcfont->data = NULL;
    }
  }
  dbc_initialized = 0;
}

/**
 * Sets a DBC Language for font initialization.
 */
void dbc_set_language(short ilng)
{
  uint8_t dbc_id = get_dbc_id(ilng);
  if (!dbc_initialized)
    dbc_language = dbc_id;
}

char * prepare_font_filename(const char * fpath, const char * fname) {
  if (fpath == NULL || fpath[0] == 0)
  {
    // current folder, copy font filename as-is
    const int fname_len = strlen(fname);
    const int buffer_size = fname_len + 1;
    char * buffer = KfxAlloc(buffer_size);
    if (buffer == NULL)
    {
      return NULL;
    }
    memcpy(buffer, fname, buffer_size);
    return buffer;
  }
  const int fpath_len = strlen(fpath);
  const int fname_len = strlen(fname);
  const int buffer_size = fpath_len + fname_len + 2;
  char * buffer = KfxAlloc(buffer_size);
  if (buffer == NULL)
  {
    return NULL;
  }
  if (fpath[fpath_len - 1] != '/')
  {
    // path does not end with /
    snprintf(buffer, buffer_size, "%s/%s", fpath, fname);
  }
  else
  {
    // path ends with /
    snprintf(buffer, buffer_size, "%s%s", fpath, fname);
  }
  return buffer;
}

short load_font_file(struct AsianFont * dbcfont, const char * fpath) {
  char * fname = prepare_font_filename(fpath, dbcfont->fname);
  if (fname == NULL)
  {
    ERRORLOG("Can't allocate memory for font filename %s", dbcfont->fname);
    return 2;
  }
  // Allocate memory for the font, dbc_shutdown will free this memory later
  dbcfont->data = KfxCalloc(dbcfont->data_length, 1);
  if (dbcfont->data == NULL)
  {
    ERRORLOG("Can't allocate memory for font %s", dbcfont->fname);
    KfxFree(fname);
    return 2;
  }
  // Load font file
  SYNCDBG(9, "Loading font \"%s\"", fname);
  TbFileHandle fhandle = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
  if (!fhandle)
  {
    ERRORLOG("Cannot open \"%s\"", fname);
    KfxFree(fname);
    return 1;
  }
  if (LbFileRead(fhandle, dbcfont->data, dbcfont->data_length) != (long) dbcfont->data_length)
  {
      ERRORLOG("Error reading %ld bytes from \"%s\"", dbcfont->data_length, fname);
      KfxFree(fname);
      return 3;
  }
  LbFileClose(fhandle);
  KfxFree(fname);
  return 0;
}

/**
 * Loads Double Byte Coding fonts from disk.
 */
short dbc_initialize(const char *fpath)
{
  const long fonts_count = dbc_fonts_count();
  struct AsianFont *dbcfonts = dbc_fonts_list();

  if (dbc_initialized)
  {
    dbc_shutdown();
  }
  for (long i = 0; i < fonts_count; i++)
  {
      const short result = load_font_file(&dbcfonts[i], fpath);
      if (result != 0) {
        dbc_shutdown();
        return result;
      }
  }
  dbc_initialized = 1;
  return 0;
}

TbBool is_dbc_language(short language)
{
    return (language == Lang_Japanese) || (language == Lang_ChineseInt) || (language == Lang_ChineseTra) || (language == Lang_Korean);
}

uint8_t get_dbc_id(short language)
{
    switch (language)
    {
    case Lang_Japanese:
        return DbcId_Japanese;
    case Lang_ChineseInt:
        return DbcId_ChineseInt;
    case Lang_ChineseTra:
        return DbcId_ChineseTra;
    case Lang_Korean:
        return DbcId_Korean;
    }
    return 0;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
