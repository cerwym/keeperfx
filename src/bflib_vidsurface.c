/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidsurface.c
 *     Graphics surfaces support.
 * @par Purpose:
 *     Surfaces used for drawing on screen.
 * @par Comment:
 *     Depends on the video support library, which is SDL in this implementation.
 * @author   Tomasz Lis
 * @date     10 Feb 2010 - 30 Sep 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "bflib_vidsurface.h"

#include "bflib_basics.h"
#include "globals.h"
#include "bflib_planar.h"
#include "bflib_video.h"
#include <SDL2/SDL.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

extern char lbDrawAreaTitle[128];
extern SDL_Window *lbWindow;

/** Internal screen surface structure. */
SDL_Surface * lbScreenSurface;
/** Internal drawing surface structure.
 *  Sometimes may be same as screen surface. */
SDL_Surface * lbDrawSurface;
/** Intermediate surface used when the draw surface needs format conversion before
 *  being scaled up to the physical window surface (e.g. 8bpp→16bpp on Vita). */
SDL_Surface * lbScaleSurface;

/******************************************************************************/
void LbScreenSurfaceInit(struct SSurface *surf)
{
  surf->surf_data = NULL;
  surf->pitch = 0;
  surf->locks_count = 0;
}

TbResult LbScreenSurfaceCreate(struct SSurface *surf,unsigned long w,unsigned long h)
{
    const SDL_PixelFormat * format = NULL;

    if (lbDrawSurface != NULL) {
        format = lbDrawSurface->format;
    }
    //SDL_HWSURFACE
    surf->surf_data = SDL_CreateRGBSurface(0 , w, h, format->BitsPerPixel,
        format->Rmask, format->Gmask, format->Bmask, format->Amask);

    if (surf->surf_data == NULL) {
        ERRORLOG("Failed to create surface.");
        return Lb_FAIL;
    }
    surf->locks_count = 0;
    surf->pitch = surf->surf_data->pitch;

    //moved color key control to blt_surface()

    return Lb_SUCCESS;
}

TbResult LbScreenSurfaceRelease(struct SSurface *surf)
{
  if (surf->surf_data == NULL) {
      return Lb_FAIL;
  }

  SDL_FreeSurface(surf->surf_data);
  surf->surf_data = NULL;

  return Lb_SUCCESS;
}

TbResult LbScreenSurfaceBlit(struct SSurface *surf, unsigned long x, unsigned long y,
    struct TbRect *rect, unsigned long blflags)
{
    // Convert TbRect to SDL rectangles
    SDL_Rect srcRect;

    srcRect.x = rect->left;
    srcRect.y = rect->top;
    srcRect.w = rect->right - rect->left;
    srcRect.h = rect->bottom - rect->top;

    SDL_Rect destRect;
    destRect.x = x;
    destRect.y = y;
    destRect.w = srcRect.w;
    destRect.h = srcRect.h;

    // Set blit parameters

    if ((blflags & 0x02) != 0) {
      //TODO: see how/if to handle this, I interpret this as "blit directly to primary rather than back"
      //secSurf = surface3;
      //I think it can simply be deleted as not even the mouse pointer code is using it and there's no way
      //to access front buffer in SDL
    }

    if ((blflags & 0x04) != 0) {
        //enable color key
        SDL_SetColorKey(surf->surf_data, SDL_TRUE, 255);
    }
    else {
        //disable color key
        SDL_SetColorKey(surf->surf_data, 0, 255);
    }

    if ((blflags & 0x10) != 0) {
        //TODO: see if this can/should be handled
        //probably it can just be deleted
        //dwTrans |= DDBLTFAST_WAIT;
    }

    // SDL has a per-surface palette for 8 bit surfaces. But the engine assumes palette
    // to be required only for screen surface. To make off-screen surface working,
    // we must manually set the palette for it. So temporarily change palette.
    SDL_Palette * paletteBackup = NULL;
    if (surf->surf_data->format->BitsPerPixel == 8) {
        paletteBackup = surf->surf_data->format->palette;
        surf->surf_data->format->palette = lbDrawSurface->format->palette;
    }

    int blresult;
    //the blit
    if ((blflags & 0x08) != 0) {
        //surface to screen
        blresult = SDL_BlitSurface(surf->surf_data, &srcRect, lbDrawSurface, &destRect);
    }
    else {
        //screen to surface
        blresult = SDL_BlitSurface(lbDrawSurface, &destRect, surf->surf_data, &srcRect);
    }

    //restore palette
    if (surf->surf_data->format->BitsPerPixel == 8) {
        surf->surf_data->format->palette = paletteBackup;
    }

    if (blresult == -1) {
        //Blitting mouse cursor will occasionally fail, so there's no point in logging this
        ERRORDBG(11,"Blit failed: %s",SDL_GetError());
        return Lb_FAIL;
    }
    return Lb_SUCCESS;
}

void *LbScreenSurfaceLock(struct SSurface *surf)
{
    if (surf->surf_data == NULL) {
        return NULL;
    }

    if (SDL_LockSurface(surf->surf_data) < 0) {
        ERRORLOG("Failed to lock surface");
        return NULL;
    }

    surf->locks_count++;
    surf->pitch = surf->surf_data->pitch;
    return surf->surf_data->pixels;
}

TbResult LbScreenSurfaceUnlock(struct SSurface *surf)
{
    if (surf->locks_count == 0) {
        return Lb_SUCCESS;
    }
    if (surf->surf_data == NULL) {
        return Lb_FAIL;
    }
    SDL_UnlockSurface(surf->surf_data);
    surf->locks_count--;
    return Lb_SUCCESS;
}

/******************************************************************************/
/* Screen-level helpers for RendererSoftware                                  */
/******************************************************************************/

TbResult LbScreenSetupRendererSurfaces(void)
{
    // Check if the window was created with SDL_WINDOW_OPENGL flag.
    // If so, we cannot use SDL_GetWindowSurface() on it — we must recreate
    // the window without the OpenGL flag to enable software rendering.
    if (lbWindow != NULL && (SDL_GetWindowFlags(lbWindow) & SDL_WINDOW_OPENGL))
    {
        SYNCLOG("LbScreenSetupRendererSurfaces: window has SDL_WINDOW_OPENGL flag, recreating for software rendering");

        // Save current window state
        int x, y, w, h;
        SDL_GetWindowPosition(lbWindow, &x, &y);
        SDL_GetWindowSize(lbWindow, &w, &h);
        Uint32 flags = SDL_GetWindowFlags(lbWindow);

        // Remove OpenGL flag and recreate the window
        flags &= ~SDL_WINDOW_OPENGL;

        SDL_DestroyWindow(lbWindow);
        lbWindow = SDL_CreateWindow(lbDrawAreaTitle, x, y, w, h, flags);

        if (!lbWindow) {
            ERRORLOG("LbScreenSetupRendererSurfaces: failed to recreate window: %s", SDL_GetError());
            return Lb_FAIL;
        }

        SDL_ShowWindow(lbWindow); // ensure it's visible
    }

    lbScreenSurface = SDL_GetWindowSurface(lbWindow);
    if (!lbScreenSurface) {
        ERRORLOG("LbScreenSetupRendererSurfaces: SDL_GetWindowSurface failed: %s", SDL_GetError());
        return Lb_FAIL;
    }

    if (lbDrawSurface->format->BitsPerPixel != lbScreenSurface->format->BitsPerPixel)
    {
        lbScaleSurface = SDL_CreateRGBSurfaceWithFormat(0,
            lbDrawSurface->w, lbDrawSurface->h,
            lbScreenSurface->format->BitsPerPixel, lbScreenSurface->format->format);
        if (!lbScaleSurface) {
            WARNLOG("LbScreenSetupRendererSurfaces: can't create scale surface: %s — direct blit will be attempted", SDL_GetError());
        }
    }
    return Lb_SUCCESS;
}

void LbScreenReleaseRendererSurfaces(void)
{
    if (lbScaleSurface) {
        SDL_FreeSurface(lbScaleSurface);
        lbScaleSurface = NULL;
    }
    /* lbScreenSurface is owned by SDL (window surface); only null it. */
    lbScreenSurface = NULL;
}

void LbScreenSwap(void)
{
    /* Refresh the window surface pointer each frame (guards against resize/alt-tab). */
    lbScreenSurface = SDL_GetWindowSurface(lbWindow);
    if (lbScreenSurface == NULL) {
        ERRORLOG("LbScreenSwap: SDL_GetWindowSurface returned NULL: %s", SDL_GetError());
        return;
    }
    SDL_Rect dst = { 0, 0, lbScreenSurface->w, lbScreenSurface->h };

    if (lbScaleSurface != NULL)
    {
        /* Two-step: convert format (e.g. 8bpp → window BPP) then scale. */
        if (SDL_BlitSurface(lbDrawSurface, NULL, lbScaleSurface, NULL) < 0) {
            ERRORLOG("LbScreenSwap: format-convert blit failed: %s", SDL_GetError());
            return;
        }
        if (SDL_BlitScaled(lbScaleSurface, NULL, lbScreenSurface, &dst) < 0) {
            ERRORLOG("LbScreenSwap: scale blit failed: %s", SDL_GetError());
            return;
        }
    }
    else if (lbDrawSurface->w != lbScreenSurface->w || lbDrawSurface->h != lbScreenSurface->h)
    {
        if (SDL_BlitScaled(lbDrawSurface, NULL, lbScreenSurface, &dst) < 0) {
            ERRORLOG("LbScreenSwap: scale blit failed: %s", SDL_GetError());
            return;
        }
    }
    else
    {
        if (SDL_BlitSurface(lbDrawSurface, NULL, lbScreenSurface, NULL) < 0) {
            ERRORLOG("LbScreenSwap: blit failed: %s", SDL_GetError());
            return;
        }
    }

    if (SDL_UpdateWindowSurface(lbWindow) < 0) {
        ERRORDBG(11, "LbScreenSwap: flip failed: %s", SDL_GetError());
    }
}

void LbScreenClearIndex(uint8_t colour_index)
{
    if (lbDrawSurface)
        SDL_FillRect(lbDrawSurface, NULL, colour_index);
}

uint8_t* LbScreenGetPixels(int* out_pitch)
{
    if (!lbDrawSurface)
        return NULL;
    if (SDL_LockSurface(lbDrawSurface) < 0)
        return NULL;
    if (out_pitch)
        *out_pitch = lbDrawSurface->pitch;
    return (uint8_t*)lbDrawSurface->pixels;
}

void LbScreenReleasePixels(void)
{
    if (lbDrawSurface)
        SDL_UnlockSurface(lbDrawSurface);
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
