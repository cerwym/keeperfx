/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererSoftware.cpp
 *     Software passthrough renderer backend implementation.
 * @par Purpose:
 *     Wraps the original SDL2-based rendering path so it satisfies the
 *     IRenderer interface. Behaviour is byte-for-byte identical to the
 *     pre-abstraction code path.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/RendererSoftware.h"

#include "renderer/backends/SoftwareWorldViewRenderer.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/backends/SoftwareUIRenderer.h"

#include "bflib_video.h"
#include "bflib_vidsurface.h"
#include "bflib_render.h"

#include <SDL2/SDL.h>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

bool RendererSoftware::Init()
{
    // Obtain the window surface. This must happen here (not in LbScreenSetup) so
    // that SDL_GetWindowSurface and SDL_CreateRenderer are never both called on the
    // same window — mixing them causes a GXM crash on Vita.
    lbScreenSurface = SDL_GetWindowSurface(lbWindow);
    if (!lbScreenSurface) {
        ERRORLOG("RendererSoftware: SDL_GetWindowSurface failed: %s", SDL_GetError());
        return false;
    }

    // If the draw surface (8bpp game buffer) and window surface have different
    // BitsPerPixel, SDL_BlitScaled requires matching BPP. Create an intermediate
    // surface in the window's pixel format: convert format first, then scale.
    if (lbDrawSurface->format->BitsPerPixel != lbScreenSurface->format->BitsPerPixel)
    {
        lbScaleSurface = SDL_CreateRGBSurfaceWithFormat(0,
            lbDrawSurface->w, lbDrawSurface->h,
            lbScreenSurface->format->BitsPerPixel, lbScreenSurface->format->format);
        if (!lbScaleSurface) {
            WARNLOG("RendererSoftware: can't create scale surface: %s — direct blit will be attempted", SDL_GetError());
        }
    }

    // Initialize transparency mapping tables for sprite rendering
    if (render_ghost == nullptr) {
        render_ghost = static_cast<unsigned char*>(KfxAlloc(65536)); // 256x256 table
        if (render_ghost == nullptr) {
            ERRORLOG("RendererSoftware: Failed to allocate render_ghost transparency table");
            return false;
        }
        // Initialize with default transparency mapping (similar to GlassMap)
        // This creates a 50% transparency effect
        for (int i = 0; i < 256; i++) {
            for (int j = 0; j < 256; j++) {
                render_ghost[i * 256 + j] = (i + j) / 2;
            }
        }
    }

    if (render_alpha == nullptr) {
        render_alpha = static_cast<unsigned char*>(KfxAlloc(65536)); // 256x256 table
        if (render_alpha == nullptr) {
            ERRORLOG("RendererSoftware: Failed to allocate render_alpha transparency table");
            KfxFree(render_ghost);
            render_ghost = nullptr;
            return false;
        }
        // Initialize with alpha blending transparency mapping
        // This creates an additive alpha blending effect
        for (int i = 0; i < 256; i++) {
            for (int j = 0; j < 256; j++) {
                int result = i + (j * 3) / 4; // 75% source, 25% destination
                render_alpha[i * 256 + j] = (result > 255) ? 255 : result;
            }
        }
    }

    // Initialize sub-renderers
    m_worldViewRenderer = new SoftwareWorldViewRenderer();
    m_mapFadePass = new SoftwareMapFadePass();
    m_textRenderer = new SoftwareTextRenderer();
    m_uiRenderer = new SoftwareUIRenderer();

    return true;
}

void RendererSoftware::Shutdown()
{
    // Free transparency mapping tables
    if (render_ghost) {
        KfxFree(render_ghost);
        render_ghost = nullptr;
    }

    if (render_alpha) {
        KfxFree(render_alpha);
        render_alpha = nullptr;
    }

    if (lbScaleSurface) {
        SDL_FreeSurface(lbScaleSurface);
        lbScaleSurface = NULL;
    }
    // lbScreenSurface is owned by SDL (window surface); do not free it.
    lbScreenSurface = NULL;

    // Clean up sub-renderers
    delete m_worldViewRenderer;
    m_worldViewRenderer = nullptr;
    delete m_mapFadePass;
    m_mapFadePass = nullptr;
    delete m_textRenderer;
    m_textRenderer = nullptr;
    delete m_uiRenderer;
    m_uiRenderer = nullptr;
}

bool RendererSoftware::BeginFrame()
{
    return true;
}

void RendererSoftware::EndFrame()
{
    // Refresh the window surface pointer each frame (guards against window resize / alt-tab).
    lbScreenSurface = SDL_GetWindowSurface(lbWindow);
    SDL_Rect dst = { 0, 0, lbScreenSurface->w, lbScreenSurface->h };

    if (lbScaleSurface != NULL)
    {
        // Two-step: convert format (e.g. 8bpp palette → window BPP) at game resolution,
        // then scale to the physical window surface. SDL_BlitScaled requires matching BPP.
        if (SDL_BlitSurface(lbDrawSurface, NULL, lbScaleSurface, NULL) < 0)
        {
            ERRORLOG("RendererSoftware::EndFrame format-convert blit failed: %s", SDL_GetError());
            return;
        }
        if (SDL_BlitScaled(lbScaleSurface, NULL, lbScreenSurface, &dst) < 0)
        {
            ERRORLOG("RendererSoftware::EndFrame scale blit failed: %s", SDL_GetError());
            return;
        }
    }
    else if (lbDrawSurface->w != lbScreenSurface->w || lbDrawSurface->h != lbScreenSurface->h)
    {
        if (SDL_BlitScaled(lbDrawSurface, NULL, lbScreenSurface, &dst) < 0)
        {
            ERRORLOG("RendererSoftware::EndFrame blit failed: %s", SDL_GetError());
            return;
        }
    }
    else
    {
        if (SDL_BlitSurface(lbDrawSurface, NULL, lbScreenSurface, NULL) < 0)
        {
            ERRORLOG("RendererSoftware::EndFrame blit failed: %s", SDL_GetError());
            return;
        }
    }

    if (SDL_UpdateWindowSurface(lbWindow) < 0)
    {
        ERRORDBG(11, "RendererSoftware::EndFrame flip failed: %s", SDL_GetError());
    }
}

uint8_t* RendererSoftware::LockFramebuffer(int* out_pitch)
{
    if (SDL_LockSurface(lbDrawSurface) < 0)
        return nullptr;

    if (out_pitch)
        *out_pitch = lbDrawSurface->pitch;

    return static_cast<uint8_t*>(lbDrawSurface->pixels);
}

void RendererSoftware::UnlockFramebuffer()
{
    SDL_UnlockSurface(lbDrawSurface);
}

const char* RendererSoftware::GetName() const
{
    return "Software";
}

bool RendererSoftware::SupportsRuntimeSwitch() const
{
    return true;
}

IWorldViewRenderer* RendererSoftware::GetWorldViewRenderer()
{
    return m_worldViewRenderer;
}

IMapFadePass* RendererSoftware::GetMapFadePass()
{
    return m_mapFadePass;
}

ITextRenderer* RendererSoftware::GetTextRenderer()
{
    return m_textRenderer;
}

IUIRenderer* RendererSoftware::GetUIRenderer()
{
    return m_uiRenderer;
}
