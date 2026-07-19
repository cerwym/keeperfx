/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidfade.c
 *     Video fading routines.
 * @par Purpose:
 *     Helper functions for fading of video screen.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     16 Jul 2010 - 05 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "vidfade.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_keybrd.h"
#include "bflib_datetm.h"
#include "bflib_dernc.h"

#include "vidmode.h"
#include "kjm_input.h"
#include "front_simple.h"
#include "player_data.h"
#include "player_instances.h"
#include "keeperfx.hpp"
#include "config_keeperfx.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool lbAdvancedFade = true;
static int lbFadeDelay = 25;

unsigned char fade_palette_in;
unsigned char frontend_palette[768];
TbRGBColorTable colours;
float g_palette_possession_tint = 0.0f;

struct TbColorTables pixmap;
struct TbAlphaTables alpha_sprite_table;
unsigned char white_pal[256];
unsigned char red_pal[256];
/******************************************************************************/
void fade_in(void)
{
    ProperFadePalette(frontend_palette, 8, Lb_PALETTE_FADE_OPEN);
}

void fade_out(void)
{
    ProperFadePalette(NULL, 8, Lb_PALETTE_FADE_CLOSED);
    RendererClearScreen(0);
}

void compute_fade_tables(struct TbColorTables *coltbl,unsigned char *spal,unsigned char *dpal)
{
    unsigned long i;
    unsigned long k;
    unsigned long r;
    unsigned long g;
    unsigned long b;
    SYNCMSG("Recomputing fade tables");
    // Intense fade to/from black - slower fade near black
    unsigned char* dst = coltbl->fade_tables;
    for (i=0; i < 32; i++)
    {
      for (k=0; k < 256; k++)
      {
        r = spal[3*k+0];
        g = spal[3*k+1];
        b = spal[3*k+2];
        *dst = LbPaletteFindColour(dpal, i * r >> 5, i * g >> 5, i * b >> 5);
        dst++;
      }
    }
    // Intense fade to/from black - faster fade part
    for (i=32; i < 192; i+=3)
    {
      for (k=0; k < 256; k++)
      {
        r = spal[3*k+0];
        g = spal[3*k+1];
        b = spal[3*k+2];
        *dst = LbPaletteFindColour(dpal, i * r >> 5, i * g >> 5, i * b >> 5);
        dst++;
      }
    }
    // Other fadings - between all the colors
    dst = coltbl->ghost;
    for (i=0; i < 256; i++)
    {
      // Reference colors
      unsigned long rr = spal[3 * i + 0];
      unsigned long rg = spal[3 * i + 1];
      unsigned long rb = spal[3 * i + 2];
      // Creating fades
      for (k=0; k < 256; k++)
      {
        r = dpal[3*k+0];
        g = dpal[3*k+1];
        b = dpal[3*k+2];
        *dst = LbPaletteFindColour(dpal, (rr+2*r) / 3, (rg+2*g) / 3, (rb+2*b) / 3);
        dst++;
      }
    }
}

void compute_alpha_table(unsigned char *alphtbl, unsigned char *spal, unsigned char *dpal, char dred, char dgreen, char dblue)
{
    int blendR = 0;
    int blendG = 0;
    int blendB = 0;
    // Every color alpha-blended with given values for 8 steps of intensity
    for (int nrow = 0; nrow < 8; nrow++)
    {
        for (int n = 0; n < 256; n++)
        {
            unsigned char* baseCol = &spal[3 * n];
            int valR = blendR + baseCol[0];
            if (valR >= 63)
              valR = 63;
            else if (valR < 0)
              valR = 0;
            int valG = blendG + baseCol[1];
            if (valG >= 63)
              valG = 63;
            else if (valG < 0)
              valG = 0;
            int valB = blendB + baseCol[2];
            if (valB >= 63)
              valB = 63;
            else if (valB < 0)
              valB = 0;

            TbPixel c = LbPaletteFindColour(dpal, valR, valG, valB);
            alphtbl[nrow*256 + n] = c;
        }
        blendR += dred;
        blendG += dgreen;
        blendB += dblue;
    }
}

void compute_alpha_tables(struct TbAlphaTables *alphtbls,unsigned char *spal,unsigned char *dpal)
{
    SYNCMSG("Recomputing alpha tables");
    {
        for (int n = 0; n < 256; n++)
        {
            alphtbls->black[n] = 144;
        }
    }
    // Every color alpha-blended with shade of white
    compute_alpha_table(alphtbls->white,  spal, dpal, 4, 4, 4);
    // Every color alpha-blended with yellow
    compute_alpha_table(alphtbls->yellow, spal, dpal, 6, 4, 0);
    // Every color alpha-blended with red
    compute_alpha_table(alphtbls->red,    spal, dpal, 6, 1, 1);
    // Every color alpha-blended with blue
    compute_alpha_table(alphtbls->blue,   spal, dpal, 2, 2, 6);
    // Every color alpha-blended with green
    compute_alpha_table(alphtbls->green,  spal, dpal, 2, 6, 2);
    // Every color alpha-blended with purple
    compute_alpha_table(alphtbls->purple, spal, dpal, 3, 0, 3);
    // Every color alpha-blended with black
    compute_alpha_table(alphtbls->black,  spal, dpal,-2,-2,-2);
    // Every color alpha-blended with orange
    compute_alpha_table(alphtbls->orange, spal, dpal, 6, 3, 1);
}

void compute_rgb2idx_table(TbRGBColorTable ctab,unsigned char *spal)
{
    SYNCMSG("Recomputing rgb-to-index tables");
    int scaler = (1 << 6) / COLOUR_TABLE_DIMENSION;
    for (int valR = 0; valR < COLOUR_TABLE_DIMENSION; valR++)
    {
        for (int valG = 0; valG < COLOUR_TABLE_DIMENSION; valG++)
        {
            for (int valB = 0; valB < COLOUR_TABLE_DIMENSION; valB++)
            {
                TbPixel c = LbPaletteFindColour(spal, scaler * valR + (scaler-1),
                    scaler * valG + (scaler-1), scaler * valB + (scaler-1));
                ctab[valR][valG][valB] = c;
            }
        }
    }
}

/**
 * Gets colours from source palette, adds given shifts to every colour and encodes it to index in destination palette.
 * @param ocol Output colours buffer.
 * @param spal Source palette, from which initial colors are taken.
 * @param dpal Destination palette, in which the output colors are coded.
 * @param shiftR Color intensity shift value, red.
 * @param shiftG Color intensity shift value, green.
 * @param shiftB Color intensity shift value, blue.
 */
void compute_shifted_palette_table(TbPixel *ocol, const unsigned char *spal, const unsigned char *dpal, int shiftR, int shiftG, int shiftB)
{
    SYNCMSG("Recomputing palette table");
    for (int i = 0; i < 256; i++)
    {
        int valR = (int)spal[3 * i + 0] + shiftR;
        if (valR >= 63) valR = 63;
        if (valR <   0) valR = 0;
        int valG = (int)spal[3 * i + 1] + shiftG;
        if (valG >= 63) valG = 63;
        if (valG <   0) valG = 0;
        int valB = (int)spal[3 * i + 2] + shiftB;
        if (valB >= 63) valB = 63;
        if (valB <   0) valB = 0;
        ocol[i] = LbPaletteFindColour(dpal, valR, valG, valB);
    }
}

/**
 * Loads a cached colour/fade table from disk, or computes and caches it if the
 * cache file is missing/stale.
 */
static TbBool load_or_compute_colour_table(const char *log_label, const char *leaf_fname,
    void *buf, size_t buf_size, void (*compute)(void))
{
    char* fname = prepare_file_path(FGrp_StdData, leaf_fname);
    SYNCDBG(0,"Reading %s file \"%s\".",log_label,fname);
    if (LbFileLoadAt(fname, buf) != (long)buf_size)
    {
        compute();
        LbFileSaveAt(fname, buf, (unsigned long)buf_size);
    }
    return true;
}

static void compute_fades_table_cb(void)
{
    compute_fade_tables(&pixmap, engine_palette, engine_palette);
}

TbBool init_fades_table(void)
{
    load_or_compute_colour_table("fade table", "tables.dat", &pixmap, sizeof(struct TbColorTables), compute_fades_table_cb);
    TbPixel cblack = 144;
    // Update black color
    for (long i = 0; i < 8192; i++)
    {
        if (pixmap.fade_tables[i] == 0) {
            pixmap.fade_tables[i] = cblack;
        }
    }
    return true;
}

static void compute_alpha_table_cb(void)
{
    compute_alpha_tables(&alpha_sprite_table, engine_palette, engine_palette);
}

TbBool init_alpha_table(void)
{
    return load_or_compute_colour_table("alpha color table", "alpha.col", &alpha_sprite_table, sizeof(struct TbAlphaTables), compute_alpha_table_cb);
}

static void compute_rgb2idx_table_cb(void)
{
    compute_rgb2idx_table(colours, engine_palette);
}

static TbBool init_rgb2idx_table(void)
{
    return load_or_compute_colour_table("rgb-to-index color table", "colours.col", &colours, sizeof(TbRGBColorTable), compute_rgb2idx_table_cb);
}

static void compute_redpal_table_cb(void)
{
    compute_shifted_palette_table(red_pal, engine_palette, engine_palette, 20, -10, -10);
}

static TbBool init_redpal_table(void)
{
    return load_or_compute_colour_table("red-blended color table", "redpal.col", &red_pal, 256, compute_redpal_table_cb);
}

static void compute_whitepal_table_cb(void)
{
    compute_shifted_palette_table(white_pal, engine_palette, engine_palette, 48, 48, 48);
}

static TbBool init_whitepal_table(void)
{
    return load_or_compute_colour_table("white-blended color table", "whitepal.col", &white_pal, 256, compute_whitepal_table_cb);
}

void init_colours(void)
{
    init_rgb2idx_table();
    init_redpal_table();
    init_whitepal_table();
}

void ProperFadePalette(unsigned char *pal, long fade_steps, enum TbPaletteFadeFlag flg)
{
/*    if (flg != Lb_PALETTE_FADE_CLOSED)
    {
        RendererPaletteFade(pal, fade_steps, flg);
    } else*/
    if (lbAdvancedFade)
    {
        RendererPreserveFadeCache(1);
        TbClockMSec latest_loop_time = LbTimerClock();
        while (RendererPaletteFade(pal, fade_steps, Lb_PALETTE_FADE_OPEN) < fade_steps)
        {
          RendererPresentFrame();
          if (!is_key_pressed(KC_SPACE,KMod_DONTCARE) &&
              !is_key_pressed(KC_ESCAPE,KMod_DONTCARE) &&
              !is_key_pressed(KC_RETURN,KMod_DONTCARE) &&
              !is_mouse_pressed_lrbutton())
          {
            latest_loop_time += lbFadeDelay;
            LbSleepUntil(latest_loop_time);
          }
        }
        RendererPreserveFadeCache(0);
    } else
    if (pal != NULL)
    {
        RendererPaletteSet(pal);
    } else
    {
        LbPaletteDataFillBlack(palette_buf);
        RendererPaletteSet(palette_buf);
    }
}

void ProperForcedFadePalette(unsigned char *pal, long fade_steps, enum TbPaletteFadeFlag flg)
{
    if (flg == Lb_PALETTE_FADE_OPEN)
    {
        RendererPaletteFade(pal, fade_steps, flg);
        return;
    }
    if (lbAdvancedFade)
    {
        RendererPreserveFadeCache(1);
        TbClockMSec latest_loop_time = LbTimerClock();
        while (RendererPaletteFade(pal, fade_steps, Lb_PALETTE_FADE_OPEN) < fade_steps)
        {
          RendererPresentFrame();
          latest_loop_time += lbFadeDelay;

          if (flag_is_set(start_params.startup_flags, (SFlg_Legal|SFlg_FX))) {
              LbSleepUntil(latest_loop_time);
          }
        }
        RendererPreserveFadeCache(0);
    } else
    if (pal != NULL)
    {
        RendererPaletteSet(pal);
    } else
    {
        memset(palette_buf, 0, sizeof(palette_buf));
        RendererPaletteSet(palette_buf);
    }
}

long PaletteFadePlayer(struct PlayerInfo *player)
{
    long i;
    // Find the fade step
    if ((player->palette_fade_step_pain != 0) && (player->palette_fade_step_possession != 0))
    {
        i = 12 * (player->palette_fade_step_pain - 1) + 10 * (player->palette_fade_step_possession - 1);
  } else
  if (player->palette_fade_step_possession != 0)
  {
    i = 2 * (5 * (player->palette_fade_step_possession-1));
  } else
  if (player->palette_fade_step_pain != 0)
  {
    i = 4 * (3 * (player->palette_fade_step_pain-1));
  } else
  { // both are == 0 - no fade
    g_palette_possession_tint = 0.0f;
    RendererSetScreenTint(0.0f, 0.0f, 0.0f, 0.0f);
    return 0;
  }
  if (i >= 120)
    i = 120;
  g_palette_possession_tint = (float)i / 120.0f;
  RendererSetScreenTint(1.0f, 0.0f, 0.0f, g_palette_possession_tint);
  long step = 120 - i;
  // Update the fade step
  if (player->palette_fade_step_pain > 0)
    player->palette_fade_step_pain--;
  if ((player->palette_fade_step_possession == 0) || (player->instance_num == PI_UnusedSlot18) || (player->instance_num == PI_UnusedSlot17))
  {
  } else
  if ((player->instance_num == PI_DirctCtrl) || (player->instance_num == PI_PsngrCtrl))
  {
    if (player->palette_fade_step_possession <= 12)
      player->palette_fade_step_possession++;
  } else
  {
    if (player->palette_fade_step_possession > 0)
      player->palette_fade_step_possession--;
  }
  RendererApplyPossessionPalette(step, player->main_palette);
  return step;
}

void PaletteApplyPainToPlayer(struct PlayerInfo *player, long intense)
{
    long i = player->palette_fade_step_pain + intense;
    if (i < 1)
        i = 1;
    else
    if (i > 10)
        i = 10;
    player->palette_fade_step_pain = i;
}


/******************************************************************************/
#ifdef __cplusplus
}
#endif
