/******************************************************************************/
// Dungeon Keeper - Platform Abstraction Layer
/******************************************************************************/
/** @file WindowSystemSDL.cpp
 *     SDL2 desktop window-system implementation of IWindowSystem.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/WindowSystemSDL.h"
#include "bflib_mouse.h"
#include "bflib_video.h"
#include <SDL2/SDL.h>
#include "post_inc.h"

// bflib_inputctrl.h declares lbMouseGrabbed / lbMouseGrab / lbAppActive.
// We keep them as the shared source of truth for now; the window system
// reads/writes through them so existing callers see consistent state.
extern "C" volatile TbBool lbAppActive;   // defined with C linkage in bflib_inputctrl.cpp
extern volatile TbBool lbMouseGrabbed;
extern volatile TbBool lbMouseGrab;
extern "C" SDL_Window* lbWindow = nullptr;  // owned here; extern declared in bflib_vidsurface.c (C linkage for C-file access)

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
    lbAppActive = true;
}

void WindowSystemSDL::OnFocusLost()
{
    m_appActive = false;
    lbAppActive = false;
}

void WindowSystemSDL::SetCursorGrab(bool grab)
{
    if (SDL_getenv("NO_RELATIVE_MOUSE") == nullptr)
    {
        SDL_SetRelativeMouseMode(grab ? SDL_TRUE : SDL_FALSE);
    }
}

void WindowSystemSDL::SetCursorVisible(bool visible)
{
    // Show the host-OS cursor only when the app does NOT have focus or the
    // cursor is not grabbed (altinput / paused on desktop).
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

void WindowSystemSDL::WarpCursor(int x, int y)
{
    SDL_WarpMouseInWindow(lbWindow, x, y);
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
    return lbWindow ? SDL_GetWindowFlags(lbWindow) : 0;
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
    return lbWindow ? SDL_GetWindowDisplayIndex(lbWindow) : -1;
}

int WindowSystemSDL::GetNumVideoDisplays() const
{
    return SDL_GetNumVideoDisplays();
}

int WindowSystemSDL::GetDesktopDisplayMode(int display, int* out_w, int* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    SDL_DisplayMode desktop;
    if (SDL_GetDesktopDisplayMode(display, &desktop) != 0)
        return -1;
    if (out_w) *out_w = desktop.w;
    if (out_h) *out_h = desktop.h;
    return 0;
}

int WindowSystemSDL::GetDisplayBounds(int display, int* out_x, int* out_y, int* out_w, int* out_h) const
{
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    SDL_Rect rect = {0, 0, 0, 0};
    if (SDL_GetDisplayBounds(display, &rect) != 0)
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
    SDL_DisplayMode desired = {SDL_PIXELFORMAT_UNKNOWN, desired_w, desired_h, 0, 0};
    SDL_DisplayMode closest = desired;
    if (SDL_GetClosestDisplayMode(display, &desired, &closest) == nullptr)
        return 0;
    if (out_w) *out_w = closest.w;
    if (out_h) *out_h = closest.h;
    return 1;
}

int WindowSystemSDL::SetWindowDisplayMode(int w, int h)
{
    if (lbWindow == nullptr)
        return -1;
    SDL_DisplayMode dm = {SDL_PIXELFORMAT_UNKNOWN, w, h, 0, 0};
    return SDL_SetWindowDisplayMode(lbWindow, &dm);
}

void WindowSystemSDL::SetWindowSize(int w, int h)
{
    if (lbWindow != nullptr)
        SDL_SetWindowSize(lbWindow, w, h);
}

int WindowSystemSDL::SetWindowFullscreen(unsigned int flags)
{
    return lbWindow ? SDL_SetWindowFullscreen(lbWindow, flags) : -1;
}

void WindowSystemSDL::SetWindowBordered(int bordered)
{
    if (lbWindow != nullptr)
        SDL_SetWindowBordered(lbWindow, bordered ? SDL_TRUE : SDL_FALSE);
}

void WindowSystemSDL::SetWindowPosition(int x, int y)
{
    if (lbWindow != nullptr)
        SDL_SetWindowPosition(lbWindow, x, y);
}

bool WindowSystemSDL::CreateWindow(const char* title, int x, int y, int w, int h, unsigned int flags)
{
    lbWindow = SDL_CreateWindow(title, x, y, w, h, flags);
    return lbWindow != nullptr;
}

bool WindowSystemSDL::RecreateForSoftwareRenderer()
{
    if (lbWindow == nullptr)
        return false;
    if (!(SDL_GetWindowFlags(lbWindow) & SDL_WINDOW_OPENGL))
        return true;

    SYNCLOG("WindowSystemSDL: removing SDL_WINDOW_OPENGL for software renderer");
    int x, y, w, h;
    SDL_GetWindowPosition(lbWindow, &x, &y);
    SDL_GetWindowSize(lbWindow, &w, &h);
    const char* title = SDL_GetWindowTitle(lbWindow);
    Uint32 flags = SDL_GetWindowFlags(lbWindow) & ~(Uint32)SDL_WINDOW_OPENGL;

    SDL_DestroyWindow(lbWindow);
    lbWindow = SDL_CreateWindow(title, x, y, w, h, flags);
    if (!lbWindow) {
        ERRORLOG("WindowSystemSDL::RecreateForSoftwareRenderer: failed: %s", SDL_GetError());
        return false;
    }
    SDL_ShowWindow(lbWindow);
    return true;
}

int WindowSystemSDL::GetDisplayRefreshRate() const
{
    if (lbWindow == nullptr)
        return 0;
    int display_index = SDL_GetWindowDisplayIndex(lbWindow);
    if (display_index < 0)
        return 0;
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(display_index, &mode) == 0 && mode.refresh_rate > 0)
        return mode.refresh_rate;
    return 0;
}
