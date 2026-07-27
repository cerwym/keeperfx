/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererHelper.cpp
 *     Shared renderer utility functions — SDL3-based implementation.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererHelper.h"

#include <SDL3/SDL.h>

#include <string.h>

#include "bflib_video.h"   // LbPaletteGetReadonly
#include "bflib_basics.h"  // ERRORLOG
#include "post_inc.h"

/******************************************************************************/

bool RendererHelper_SaveIndexedImage(const uint8_t* pixels, int w, int h, int pitch,
                                     const char* path)
{
    // SDL3: SDL_CreateSurfaceFrom(w, h, format, pixels, pitch) — note different parameter order
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_INDEX8, (void*)pixels, pitch);
    if (!surf) {
        ERRORLOG("RendererHelper_SaveIndexedImage: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
        return false;
    }

    // Build SDL palette from the active palette (6-bit DK values, shifted to 8-bit).
    const unsigned char* pal = LbPaletteGetReadonly();
    SDL_Color colors[256];
    for (int i = 0; i < 256; i++) {
        colors[i].r = (pal[i * 3 + 0] & 0x3F) << 2;
        colors[i].g = (pal[i * 3 + 1] & 0x3F) << 2;
        colors[i].b = (pal[i * 3 + 2] & 0x3F) << 2;
        colors[i].a = 255;
    }
    // SDL3: palette access via SDL_GetSurfacePalette (surf->format is now an enum, not a struct)
    SDL_SetPaletteColors(SDL_GetSurfacePalette(surf), colors, 0, 256);

    bool ok = false;
#ifndef PLATFORM_VITA
    const char* ext = strrchr(path, '.');
    if (ext && SDL_strcasecmp(ext, ".png") == 0) {
        ok = SDL_SavePNG(surf, path);
    } else {
        ok = SDL_SaveBMP(surf, path);
    }
#else
    ok = SDL_SaveBMP(surf, path);
#endif
    if (!ok) {
        ERRORLOG("RendererHelper_SaveIndexedImage: failed to save '%s': %s", path, SDL_GetError());
    }

    SDL_DestroySurface(surf);
    return ok;
}

bool RendererHelper_SaveRGBAImage(const uint8_t* pixels, int w, int h, int pitch,
                                  int fmt, const char* path)
{
    // SDL3: SDL_CreateSurfaceFrom(w, h, format, pixels, pitch)
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void*)pixels, pitch);
    if (!surf) {
        ERRORLOG("RendererHelper_SaveRGBAImage: SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
        return false;
    }
    bool ok = false;
#ifndef PLATFORM_VITA
    ok = (fmt == 2)
        ? SDL_SaveBMP(surf, path)
        : SDL_SavePNG(surf, path);
#else
    ok = SDL_SaveBMP(surf, path);
#endif
    if (!ok)
        ERRORLOG("RendererHelper_SaveRGBAImage: failed to save '%s': %s", path, SDL_GetError());
    SDL_DestroySurface(surf);
    return ok;
}
