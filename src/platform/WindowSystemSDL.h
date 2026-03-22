#ifndef WINDOW_SYSTEM_SDL_H
#define WINDOW_SYSTEM_SDL_H

#include "platform/IWindowSystem.h"

/** SDL2 desktop window-system implementation.
 *
 *  Used by PlatformWindows and PlatformLinux.  Wraps SDL focus events,
 *  relative mouse mode, cursor visibility, and window warp — all the
 *  SDL-specific cursor/focus concerns that previously lived directly in
 *  bflib_inputctrl.cpp and bflib_mouse.cpp.
 *
 *  The single global instance is shared by both desktop platform classes
 *  (there is only one SDL window at a time anyway).
 */
class WindowSystemSDL : public IWindowSystem {
public:
    // ----- Focus -----
    bool IsAppActive() const override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    // ----- OS cursor -----
    bool HasOSCursor() const override { return true; }
    void SetCursorGrab(bool grab) override;
    void SetCursorVisible(bool visible) override;
    void WarpCursor(int x, int y) override;

    // PollInput is a no-op: SDL delivers mouse input via events.

private:
    bool m_appActive = true;
};

/** Shared singleton — both PlatformWindows and PlatformLinux return this. */
WindowSystemSDL* GetSDLWindowSystem();

#endif // WINDOW_SYSTEM_SDL_H
