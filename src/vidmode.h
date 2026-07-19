/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidmode.h
 *     Header file for vidmode.c.
 *     Note that this file is a C header, while its code is CPP.
 * @par Purpose:
 *     Video mode switching/setting function.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_VIDMODE_H
#define DK_VIDMODE_H

#include "bflib_basics.h"
#include "globals.h"

#include "bflib_video.h"
#include "bflib_filelst.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_GAME_VIDMODE_COUNT 6 /**< the size of the switching_vidmodes array. */

/******************************************************************************/
extern struct TbLoadFiles legal_load_files[];
extern struct TbLoadFilesV2 game_load_files[];
extern unsigned short units_per_pixel_min;
extern long base_mouse_sensitivity;

extern TbBool MinimalResolutionSetup;
/******************************************************************************/
void switch_to_next_video_mode_wrapper(void);
void set_game_vidmode(uint i, TbScreenMode nmode);
TbScreenMode reenter_video_mode(void);
TbScreenMode get_movies_vidmode(void);
TbScreenMode get_frontend_vidmode(void);
void set_failsafe_vidmode(TbScreenMode nmode);
void set_movies_vidmode(TbScreenMode nmode);
void set_frontend_vidmode(TbScreenMode nmode);
char *get_vidmode_name(TbScreenMode mode);

TbScreenMode setup_screen_mode_minimal(TbScreenMode nmode);
TbScreenMode setup_screen_mode_zero(TbScreenMode nmode);

TbBool update_screen_mode_data(long width, long height);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
