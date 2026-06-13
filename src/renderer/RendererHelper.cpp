/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererHelper.cpp
 *     Shared renderer utility functions — SDL2-based implementation.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererHelper.h"

#ifndef PLATFORM_3DS

#include <SDL2/SDL.h>
#ifndef PLATFORM_VITA
#  include <SDL2/SDL_image.h>
#endif
#include <string.h>

#include "bflib_video.h"   // lbPalette
#include "bflib_basics.h"  // ERRORLOG
#include "post_inc.h"

/******************************************************************************/

bool RendererHelper_SaveIndexedImage(const uint8_t* pixels, int w, int h, int pitch,
                                     const char* path)
{
    // Wrap pixel data in a temporary 8bpp surface (no copy).
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(
        (void*)pixels, w, h, 8, pitch, 0, 0, 0, 0);
    if (!surf) {
        ERRORLOG("RendererHelper_SaveIndexedImage: SDL_CreateRGBSurfaceFrom failed: %s", SDL_GetError());
        return false;
    }

    // Build SDL palette from lbPalette (6-bit DK values, shifted to 8-bit).
    SDL_Color colors[256];
    for (int i = 0; i < 256; i++) {
        colors[i].r = (lbPalette[i * 3 + 0] & 0x3F) << 2;
        colors[i].g = (lbPalette[i * 3 + 1] & 0x3F) << 2;
        colors[i].b = (lbPalette[i * 3 + 2] & 0x3F) << 2;
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(surf->format->palette, colors, 0, 256);

    bool ok = false;
#ifndef PLATFORM_VITA
    const char* ext = strrchr(path, '.');
    if (ext && SDL_strcasecmp(ext, ".png") == 0) {
        ok = (IMG_SavePNG(surf, path) == 0);
    } else {
        ok = (SDL_SaveBMP(surf, path) == 0);
    }
#else
    // SDL_image not available on Vita; save as BMP regardless of extension.
    ok = (SDL_SaveBMP(surf, path) == 0);
#endif
    if (!ok) {
        ERRORLOG("RendererHelper_SaveIndexedImage: failed to save '%s': %s", path, SDL_GetError());
    }

    SDL_FreeSurface(surf);
    return ok;
}

bool RendererHelper_SaveRGBAImage(const uint8_t* pixels, int w, int h, int pitch,
                                  int fmt, const char* path)
{
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)pixels, w, h, 32, pitch, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        ERRORLOG("RendererHelper_SaveRGBAImage: SDL_CreateRGBSurfaceWithFormatFrom failed: %s",
                 SDL_GetError());
        return false;
    }
    bool ok = false;
#ifndef PLATFORM_VITA
    ok = (fmt == 2)
        ? (SDL_SaveBMP(surf, path) == 0)
        : (IMG_SavePNG(surf, path) == 0);
#else
    ok = (SDL_SaveBMP(surf, path) == 0);
#endif
    if (!ok)
        ERRORLOG("RendererHelper_SaveRGBAImage: failed to save '%s': %s", path, SDL_GetError());
    SDL_FreeSurface(surf);
    return ok;
}

#endif // PLATFORM_3DS
