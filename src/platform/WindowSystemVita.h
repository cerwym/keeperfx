#ifndef WINDOW_SYSTEM_VITA_H
#define WINDOW_SYSTEM_VITA_H

#ifdef PLATFORM_VITA

#include "platform/IWindowSystem.h"

/** PS Vita window-system implementation of IWindowSystem.
 *
 *  The Vita owns the display exclusively — there is no OS windowing concept
 *  and no host cursor.  Focus is always "active".  SetCursorGrab and
 *  SetCursorVisible are no-ops.
 *
 *  PollInput() is the key method: it reads the virtual cursor position
 *  maintained by input_vita.c (driven by the analog stick and front
 *  touchscreen) and injects a MOUSEMOVE event into the engine's mouse
 *  handler so the in-game cursor actually moves.
 */
class WindowSystemVita : public IWindowSystem {
public:
    bool IsAppActive() const override { return true; }

    // No focus events on a console — no-ops.
    void OnFocusGained() override {}
    void OnFocusLost() override {}

    bool HasOSCursor() const override { return false; }

    // No OS cursor to grab/show/warp.
    void SetCursorGrab(bool) override {}
    void SetCursorVisible(bool) override {}
    void WarpCursor(int, int) override {}

    /** Reads the virtual cursor from input_vita and injects a MOUSEMOVE so
     *  the engine's lbDisplay.MMouseX/Y tracks the analog/touch position. */
    void PollInput() override;
};

#endif // PLATFORM_VITA
#endif // WINDOW_SYSTEM_VITA_H
