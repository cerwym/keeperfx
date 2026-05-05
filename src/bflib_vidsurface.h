/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidsurface.h
 *     Header file for bflib_vidsurface.c.
 * @par Purpose:
 *     Graphics surfaces support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     10 Feb 2010 - 30 Sep 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_VIDSURFACE_H
#define BFLIB_VIDSURFACE_H

#include "bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct SDL_Surface;
struct TbRect;

struct SSurface {
    struct SDL_Surface * surf_data;
    unsigned long locks_count;
    long pitch;
};
/******************************************************************************/
extern struct SDL_Surface * lbScreenSurface;
extern struct SDL_Surface * lbDrawSurface;
extern struct SDL_Surface * lbScaleSurface;
extern volatile TbBool lbHasSecondSurface;
/******************************************************************************/
void LbScreenSurfaceInit(struct SSurface *surf);
TbResult LbScreenSurfaceCreate(struct SSurface *surf, unsigned long w, unsigned long h);
TbResult LbScreenSurfaceRelease(struct SSurface *surf);
TbResult LbScreenSurfaceBlit(struct SSurface *surf, unsigned long x, unsigned long y,
    struct TbRect *rect, unsigned long blflags);
void *LbScreenSurfaceLock(struct SSurface *surf);
TbResult LbScreenSurfaceUnlock(struct SSurface *surf);

/** Obtain the SDL window surface and create an intermediate scale surface when
 *  the draw surface BPP differs from the window BPP.  Call once from
 *  RendererSoftware::Init() after the SDL window exists.
 *  Returns Lb_SUCCESS on success or Lb_FAIL if the window surface is unavailable. */
TbResult LbScreenSetupRendererSurfaces(void);

/** Release resources allocated by LbScreenSetupRendererSurfaces().
 *  lbScreenSurface is owned by SDL and is only nulled, not freed. */
void LbScreenReleaseRendererSurfaces(void);

/** Present the completed frame: runs the blit chain and calls SDL_UpdateWindowSurface.
 *  Refreshes the window surface pointer each call (guards against resize/alt-tab). */
void LbScreenSwap(void);

/** Fill the draw surface with colour_index (palette index, not RGB). */
void LbScreenClearIndex(uint8_t colour_index);

/** Lock the draw surface for CPU pixel writes.
 *  Returns the pixel pointer and writes the surface pitch to *out_pitch.
 *  Returns NULL on failure. */
uint8_t* LbScreenGetPixels(int* out_pitch);

/** Unlock the draw surface after CPU pixel writes. */
void LbScreenReleasePixels(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
