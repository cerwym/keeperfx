/******************************************************************************/
// Dungeon Keeper - Platform Abstraction Layer
/******************************************************************************/
/** @file WindowSystemSDL.cpp
 *     SDL3 desktop window-system implementation of IWindowSystem.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/WindowSystemSDL.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include <SDL3/SDL.h>
#include "post_inc.h"

// bflib_inputctrl.h declares lbMouseGrabbed / lbMouseGrab.
// We keep them as the shared source of truth for now; the window system
// reads/writes through them so existing callers see consistent state.
extern volatile TbBool lbMouseGrabbed;
extern volatile TbBool lbMouseGrab;
extern "C" SDL_Window* lbWindow = nullptr;  // owned here; extern declared in bflib_video.h

/******************************************************************************/

static WindowSystemSDL s_sdlWindowSystem;

WindowSystemSDL* GetSDLWindowSystem()
{
    return &s_sdlWindowSystem;
}

bool WindowSystemSDL::IsAppActive() const
{
    return m_appActive;
}

void WindowSystemSDL::OnFocusGained()
{
    m_appActive = true;
}

void WindowSystemSDL::OnFocusLost()
{
    m_appActive = false;
}

void WindowSystemSDL::SetCursorGrab(bool grab)
{
    if (SDL_getenv("NO_RELATIVE_MOUSE") == nullptr)
    {
        SDL_SetWindowRelativeMouseMode(lbWindow, grab);
    }
}

void WindowSystemSDL::SetCursorVisible(bool visible)
{
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void WindowSystemSDL::WarpCursor(int x, int y)
{
    SDL_WarpMouseInWindow(lbWindow, (float)x, (float)y);
}

bool WindowSystemSDL::HasWindow() const
{
    return lbWindow != nullptr;
}

SDL_Window* WindowSystemSDL::GetSDLWindow() const
{
    return lbWindow;
}

unsigned int WindowSystemSDL::GetWindowFlags() const
{
    if (!lbWindow)
        return 0;
    SDL_WindowFlags sdl_flags = SDL_GetWindowFlags(lbWindow);
    unsigned int kfx_flags = 0;

    // Translate SDL3 window flags to our KFX/SDL2-compat flag set.
    if (m_desktopFakeFullscreen)
    {
        // DESKTOP mode is implemented as a plain borderless window (no SDL_WINDOW_FULLSCREEN)
        // to keep Windows FSO inactive and preserve the DWM HDR compositor.
        // Report it as SDL_WINDOW_FULLSCREEN_DESKTOP so callers treat it the same way.
        kfx_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    else if (sdl_flags & SDL_WINDOW_FULLSCREEN)
    {
        // SDL3 uses SDL_GetWindowFullscreenMode to distinguish desktop vs exclusive:
        // NULL  → desktop fullscreen (borderless, no mode change)
        // !NULL → exclusive fullscreen (specific display mode set)
        if (SDL_GetWindowFullscreenMode(lbWindow) == nullptr)
            kfx_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;  // 0x1001 compat value
        else
            kfx_flags |= 0x00000001u;  // SDL_WINDOW_FULLSCREEN (exclusive)
    }
    // SDL_WINDOW_BORDERLESS is a separate property only for plain borderless windows.
    // For fake-fullscreen (DESKTOP mode) the window IS borderless at the SDL level,
    // but we don't report it separately — the borderless state is implied by
    // SDL_WINDOW_FULLSCREEN_DESKTOP.  If we reported it, LbScreenSetup would compare
    // cur_bordered=0 against new_bordered=1 (mdinfo->sdlFlags has no BORDERLESS bit
    // for DESKTOP mode) and mistakenly call SetWindowBordered(1) on our fake-fullscreen window.
    if (!m_desktopFakeFullscreen && (sdl_flags & SDL_WINDOW_BORDERLESS))
        kfx_flags |= SDL_WINDOW_BORDERLESS;
    if (sdl_flags & SDL_WINDOW_OPENGL)      kfx_flags |= SDL_WINDOW_OPENGL;
    if (sdl_flags & SDL_WINDOW_HIDDEN)      kfx_flags |= SDL_WINDOW_HIDDEN;
    if (sdl_flags & SDL_WINDOW_VULKAN)      kfx_flags |= SDL_WINDOW_VULKAN;

    return kfx_flags;
}

void WindowSystemSDL::GetWindowSize(int* out_w, int* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (lbWindow == nullptr)
        return;
    SDL_GetWindowSize(lbWindow, out_w, out_h);
}

int WindowSystemSDL::GetWindowDisplayIndex() const
{
    if (lbWindow)
        return (int)SDL_GetDisplayForWindow(lbWindow);
    // No window yet — return the primary display so callers can query desktop modes.
    return (int)SDL_GetPrimaryDisplay();
}

int WindowSystemSDL::GetNumVideoDisplays() const
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays)
        SDL_free(displays);
    return count;
}

// Translate a 0-based display index (as used by the FILLALL loop in bflib_video.c)
// into an SDL3 display ID. Returns SDL_GetPrimaryDisplay() as fallback.
static SDL_DisplayID display_index_to_id(int index)
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID id = (displays && index >= 0 && index < count)
                       ? displays[index]
                       : SDL_GetPrimaryDisplay();
    if (displays) SDL_free(displays);
    return id;
}

int WindowSystemSDL::GetDesktopDisplayMode(int display, int* out_w, int* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    // SDL3 display IDs start at 1; 0 means "not set yet" — fall back to primary.
    SDL_DisplayID display_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display_id);
    if (!mode)
        return -1;
    if (out_w) *out_w = mode->w;
    if (out_h) *out_h = mode->h;
    return 0;
}

int WindowSystemSDL::GetDisplayBounds(int display, int* out_x, int* out_y, int* out_w, int* out_h) const
{
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    // Caller passes a 0-based index — translate to SDL3 display ID.
    SDL_DisplayID display_id = display_index_to_id(display);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!SDL_GetDisplayBounds(display_id, &rect))
        return -1;
    if (out_x) *out_x = rect.x;
    if (out_y) *out_y = rect.y;
    if (out_w) *out_w = rect.w;
    if (out_h) *out_h = rect.h;
    return 0;
}

int WindowSystemSDL::GetClosestDisplayMode(int display, int desired_w, int desired_h, int* out_w, int* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    // Caller passes an SDL3 display ID; 0 means not set yet — fall back to primary.
    SDL_DisplayID display_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    SDL_DisplayMode closest = {};
    if (!SDL_GetClosestFullscreenDisplayMode(display_id, desired_w, desired_h, 0.0f, false, &closest))
        return 0;
    if (out_w) *out_w = closest.w;
    if (out_h) *out_h = closest.h;
    return 1;
}

int WindowSystemSDL::SetWindowDisplayMode(int w, int h)
{
    if (lbWindow == nullptr)
        return -1;
    // On HDR displays, exclusive fullscreen forces the OS to switch the display
    // pipeline (DWM HDR compositor → direct scanout), producing a visible mode
    // change and breaking OBS window capture.  Use desktop/borderless fullscreen
    // (null mode) so DWM stays active, handles SDR tone-mapping transparently,
    // and OBS can capture the window normally.
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(lbWindow);
    if (display_id != 0) {
        bool hdr = SDL_GetBooleanProperty(
            SDL_GetDisplayProperties(display_id),
            SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
        if (hdr) {
            SYNCLOG("HDR display detected — using borderless fullscreen to avoid mode switch");
            SDL_SetWindowFullscreenMode(lbWindow, nullptr);
            return 0;
        }
    }
    // SDR display: use the requested exclusive fullscreen mode.
    SDL_DisplayMode dm = {};
    dm.w = w;
    dm.h = h;
    return SDL_SetWindowFullscreenMode(lbWindow, &dm) ? 0 : -1;
}

void WindowSystemSDL::SetWindowSize(int w, int h)
{
    if (lbWindow != nullptr)
        SDL_SetWindowSize(lbWindow, w, h);
}

int WindowSystemSDL::SetWindowFullscreen(unsigned int flags)
{
    if (!lbWindow)
        return -1;
    SYNCLOG("SetWindowFullscreen: flags=0x%08X  cur_desktopFakeFS=%d", flags, m_desktopFakeFullscreen ? 1 : 0);
    if (flags == 0)
    {
        // Leave fullscreen → windowed
        m_desktopFakeFullscreen = false;
        return SDL_SetWindowFullscreen(lbWindow, false) ? 0 : -1;
    }
    if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP)
    {
        // Fake fullscreen: borderless window covering the display, no SDL_WINDOW_FULLSCREEN.
        // Avoids Windows FSO / Independent Flip which would bypass the DWM HDR compositor.
        if (!m_desktopFakeFullscreen)
        {
            SDL_SetWindowFullscreen(lbWindow, false);   // exit any prior SDL fullscreen
            SDL_SetWindowBordered(lbWindow, false);
            SDL_DisplayID disp = SDL_GetDisplayForWindow(lbWindow);
            if (disp != 0)
            {
                SDL_Rect bounds = {};
                SDL_GetDisplayBounds(disp, &bounds);
                SDL_SetWindowSize(lbWindow, bounds.w, bounds.h);
                SDL_SetWindowPosition(lbWindow, bounds.x, bounds.y);
            }
            m_desktopFakeFullscreen = true;
        }
        return 0;
    }
    // Exclusive fullscreen requested.  On HDR displays, SDL_WINDOW_FULLSCREEN
    // triggers Windows Fullscreen Optimizations (FSO) which bypasses the DWM
    // HDR compositor and switches the monitor to SDR mode.  Upgrade to fake
    // fullscreen instead so the HDR compositor stays active.
    {
        SDL_DisplayID disp = SDL_GetDisplayForWindow(lbWindow);
        bool hdr = (disp != 0) && SDL_GetBooleanProperty(
            SDL_GetDisplayProperties(disp),
            SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
        if (hdr)
        {
            SYNCLOG("HDR display: upgrading exclusive fullscreen request to fake fullscreen to preserve HDR compositor");
            if (!m_desktopFakeFullscreen)
            {
                SDL_SetWindowBordered(lbWindow, false);
                SDL_Rect bounds = {};
                if (disp != 0)
                {
                    SDL_GetDisplayBounds(disp, &bounds);
                    SDL_SetWindowSize(lbWindow, bounds.w, bounds.h);
                    SDL_SetWindowPosition(lbWindow, bounds.x, bounds.y);
                }
                m_desktopFakeFullscreen = true;
            }
            return 0;
        }
    }
    m_desktopFakeFullscreen = false;
    return SDL_SetWindowFullscreen(lbWindow, true) ? 0 : -1;
}

void WindowSystemSDL::SetWindowBordered(int bordered)
{
    if (lbWindow != nullptr)
        SDL_SetWindowBordered(lbWindow, bordered ? true : false);
}

void WindowSystemSDL::SetWindowPosition(int x, int y)
{
    if (lbWindow != nullptr)
        SDL_SetWindowPosition(lbWindow, x, y);
}

bool WindowSystemSDL::CreateWindow(const char* title, int x, int y, int w, int h, unsigned int flags)
{
    // Translate KFX/SDL2-compat flags to SDL3 window creation flags.
    SDL_WindowFlags sdl3_flags = 0;
    bool is_desktop_fs = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;

    if (is_desktop_fs)
    {
        // Fake fullscreen: a plain borderless window sized to the display.
        // We deliberately avoid SDL_WINDOW_FULLSCREEN so that Windows does NOT
        // classify the process as a "fullscreen game" and engage Fullscreen
        // Optimizations (FSO) / Independent Flip.  When FSO activates it bypasses
        // the DWM HDR compositor, switching the monitor from HDR to SDR mode.
        // A borderless window stays in the DWM composition path, so HDR remains
        // active and OBS window-capture works correctly.
        sdl3_flags |= SDL_WINDOW_BORDERLESS;
        m_desktopFakeFullscreen = true;
        SYNCLOG("DESKTOP mode: fake fullscreen (no SDL_WINDOW_FULLSCREEN) to preserve HDR compositor");
    }
    else
    {
        if (flags & 0x00000001u /*SDL_WINDOW_FULLSCREEN*/) sdl3_flags |= SDL_WINDOW_FULLSCREEN;
        if (flags & SDL_WINDOW_BORDERLESS)                 sdl3_flags |= SDL_WINDOW_BORDERLESS;
        m_desktopFakeFullscreen = false;
    }
    if (flags & SDL_WINDOW_OPENGL)  sdl3_flags |= SDL_WINDOW_OPENGL;
    if (flags & SDL_WINDOW_HIDDEN)  sdl3_flags |= SDL_WINDOW_HIDDEN;
    if (flags & SDL_WINDOW_VULKAN)  sdl3_flags |= SDL_WINDOW_VULKAN;

    // SDL3 CreateWindow no longer takes x,y — set position separately.
    lbWindow = SDL_CreateWindow(title, w, h, sdl3_flags);
    if (!lbWindow)
        return false;

    // Position: SDL3 handles placement for SDL_WINDOW_FULLSCREEN internally.
    // For fake fullscreen and windowed modes we apply the requested position.
    if (!(sdl3_flags & SDL_WINDOW_FULLSCREEN))
        SDL_SetWindowPosition(lbWindow, x, y);

    return true;
}

bool WindowSystemSDL::RecreateForSoftwareRenderer()
{
    if (lbWindow == nullptr)
        return false;
    SDL_WindowFlags cur_flags = SDL_GetWindowFlags(lbWindow);
    if (!(cur_flags & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN)))
        return true;

    SYNCLOG("WindowSystemSDL: removing SDL_WINDOW_OPENGL/VULKAN for software renderer");
    int w = 0, h = 0;
    SDL_GetWindowSize(lbWindow, &w, &h);
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_GetWindowPosition(lbWindow, &x, &y);
    // SDL_GetWindowTitle returns memory OWNED by the window; SDL_DestroyWindow
    // frees it.  Copy it first, or SDL_CreateWindow reads a dangling pointer and
    // the new window gets a garbage title.
    // This'll also make it easier to do window title updating like i saw in Ironwail, (discord rich presnce style)
    char title[256];
    { const char* cur = SDL_GetWindowTitle(lbWindow); snprintf(title, sizeof(title), "%s", cur ? cur : ""); }
    SDL_WindowFlags new_flags = cur_flags & ~(SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN);

    SDL_DestroyWindow(lbWindow);
    lbWindow = SDL_CreateWindow(title, w, h, new_flags);
    if (!lbWindow) {
        ERRORLOG("WindowSystemSDL::RecreateForSoftwareRenderer: failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(lbWindow, x, y);
    SDL_ShowWindow(lbWindow);
    return true;
}

bool WindowSystemSDL::RecreateForVulkanRenderer()
{
    if (lbWindow == nullptr)
        return false;
    if (!(SDL_GetWindowFlags(lbWindow) & SDL_WINDOW_VULKAN))
        return true;

    SYNCLOG("WindowSystemSDL: removing SDL_WINDOW_VULKAN");
    int w = 0, h = 0;
    SDL_GetWindowSize(lbWindow, &w, &h);
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_GetWindowPosition(lbWindow, &x, &y);
    // Copy the title before SDL_DestroyWindow frees the SDL_GetWindowTitle
    // buffer (see RecreateForSoftwareRenderer) — avoids a dangling-pointer title.
    char title[256];
    { const char* cur = SDL_GetWindowTitle(lbWindow); snprintf(title, sizeof(title), "%s", cur ? cur : ""); }
    SDL_WindowFlags new_flags = SDL_GetWindowFlags(lbWindow) & ~(SDL_WindowFlags)SDL_WINDOW_VULKAN;

    SDL_DestroyWindow(lbWindow);
    lbWindow = SDL_CreateWindow(title, w, h, new_flags);
    if (!lbWindow) {
        ERRORLOG("WindowSystemSDL::RecreateForVulkanRenderer: failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(lbWindow, x, y);
    SDL_ShowWindow(lbWindow);
    return true;
}

int WindowSystemSDL::GetDisplayRefreshRate() const
{
    if (lbWindow == nullptr)
        return 0;
    SDL_DisplayID display_id = SDL_GetDisplayForWindow(lbWindow);
    if (display_id == 0)
        return 0;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display_id);
    if (mode && mode->refresh_rate > 0)
        return (int)mode->refresh_rate;
    return 0;
}
