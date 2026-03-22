#ifndef IWINDOWSYSTEM_H
#define IWINDOWSYSTEM_H

/** Abstract interface for platform windowing, focus, and OS cursor management.
 *
 *  Obtained via IPlatform::GetWindowSystem().  Provides safe defaults for
 *  platforms that have no OS windowing system (consoles) — all methods are
 *  non-pure with no-op or constant-true defaults so that 3DS, Switch, and
 *  other stub platforms require zero implementation.
 *
 *  SDL desktops override this with WindowSystemSDL.
 *  Vita overrides this with WindowSystemVita.
 *
 *  The key contract that consumer code must respect:
 *    if (!GetWindowSystem()->HasOSCursor()) return;  // in any grab-policy function
 */
class IWindowSystem {
public:
    virtual ~IWindowSystem() = default;

    // ----- Focus / activity -----

    /** Returns true if the application window currently has OS focus.
     *  On consoles that own the display exclusively this always returns true. */
    virtual bool IsAppActive() const { return true; }

    /** Called by the SDL event loop when the window gains focus.
     *  No-op on platforms without a windowing system. */
    virtual void OnFocusGained() {}

    /** Called by the SDL event loop when the window loses focus.
     *  No-op on platforms without a windowing system. */
    virtual void OnFocusLost() {}

    // ----- OS cursor capability -----

    /** Returns true if the platform has a real OS-managed cursor that can be
     *  grabbed, hidden, and warped (e.g. SDL desktop).  Returns false on
     *  console platforms where the "cursor" is a virtual game-layer concept
     *  driven by touch or controller input.
     *
     *  Any function that applies OS cursor policy (LbGrabMouseCheck,
     *  LbSetMouseGrab, IsMouseInsideWindow) should early-return when this
     *  returns false. */
    virtual bool HasOSCursor() const { return false; }

    /** Grab or release the OS cursor (relative mouse mode on SDL).
     *  No-op when HasOSCursor() is false. */
    virtual void SetCursorGrab(bool /*grab*/) {}

    /** Show or hide the OS cursor.
     *  No-op when HasOSCursor() is false. */
    virtual void SetCursorVisible(bool /*visible*/) {}

    /** Warp the cursor to (x, y) in game-surface coordinates.
     *  SDL: calls SDL_WarpMouseInWindow.
     *  Vita: clamps and stores the virtual cursor position.
     *  No-op on platforms where warping is meaningless. */
    virtual void WarpCursor(int /*x*/, int /*y*/) {}

    // ----- Per-frame poll -----

    /** Called once per event-poll cycle from LbWindowsControl().
     *  SDL: no-op (input arrives via SDL events).
     *  Vita: reads the virtual cursor from the input layer and injects a
     *        MOUSEMOVE event into the engine's mouse handler so the in-game
     *        cursor follows the analog stick / touch. */
    virtual void PollInput() {}
};

#endif // IWINDOWSYSTEM_H
