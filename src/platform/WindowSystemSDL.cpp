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
extern SDL_Window*   lbWindow;      // declared in bflib_video.h

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
