#ifndef WINDOW_SYSTEM_SDL_H
#define WINDOW_SYSTEM_SDL_H

#include "platform/IWindowSystem.h"

struct SDL_Window;  // forward declaration; full type in WindowSystemSDL.cpp

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

    // ----- Window management -----
    bool HasWindow() const override;
    SDL_Window* GetSDLWindow() const;
    unsigned int GetWindowFlags() const override;
    void GetWindowSize(int* out_w, int* out_h) const override;
    int GetWindowDisplayIndex() const override;
    int GetNumVideoDisplays() const override;
    int GetDesktopDisplayMode(int display, int* out_w, int* out_h) const override;
    int GetDisplayBounds(int display, int* out_x, int* out_y, int* out_w, int* out_h) const override;
    int GetClosestDisplayMode(int display, int desired_w, int desired_h, int* out_w, int* out_h) const override;
    int SetWindowDisplayMode(int w, int h) override;
    void SetWindowSize(int w, int h) override;
    int SetWindowFullscreen(unsigned int flags) override;
    void SetWindowBordered(int bordered) override;
    void SetWindowPosition(int x, int y) override;
    bool CreateWindow(const char* title, int x, int y, int w, int h, unsigned int flags) override;
    bool RecreateForSoftwareRenderer() override;
    bool RecreateForVulkanRenderer() override;

    // ----- Display info -----
    int GetDisplayRefreshRate() const override;

    // PollInput is a no-op: SDL delivers mouse input via events.

private:
    bool m_appActive = true;
};

/** Shared singleton — both PlatformWindows and PlatformLinux return this. */
WindowSystemSDL* GetSDLWindowSystem();

#endif // WINDOW_SYSTEM_SDL_H
