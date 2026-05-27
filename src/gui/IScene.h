/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file IScene.h
 *     IScene — base class for all scenes on the SceneManager stack.
 *
 *     Lifecycle:
 *       onPush()    — called when scene is pushed onto the stack.
 *       onPop()     — called just before the scene is removed (destructor
 *                     follows immediately).  Release GPU resources here.
 *       onPause()   — called when another scene is pushed on top.
 *       onResume()  — called when the scene above is popped and this becomes
 *                     top again.
 *
 *     Per-frame:
 *       tick(dt, view)        — logic update.  SceneManager respects m_tick_hz:
 *                               if non-zero, tick() is called at most m_tick_hz
 *                               times per second (accumulator-based).  dt is
 *                               the time since the last tick FOR THIS SCENE.
 *       draw(ctx, view)       — rendering.  Called every renderer frame
 *                               regardless of tick rate.
 *       handleInput(ev, view) — input.  Only called on the top-of-stack scene.
 *                               Return true if the event was consumed (stops
 *                               propagation), false to pass through to the
 *                               scene below.
 *
 *     All scenes draw bottom-to-top every frame so a sub-scene composites
 *     on top of its parent (e.g. ParchmentSubScene on top of GameScene).
 *     Only the top-of-stack scene receives input, giving natural isolation.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "gui/DrawContext.h"
#include "gui/ClientViewState.h"

struct PlayerInfo;

/// Minimal input event forwarded from the legacy input system.
/// Expand as more event types are migrated.
struct InputEvent
{
    enum Type { None, MouseMove, MouseDown, MouseUp, KeyDown, KeyUp } type = None;
    int key   = 0;      ///< Key scancode for Key* events
    int x     = 0;      ///< Mouse pixel X
    int y     = 0;      ///< Mouse pixel Y
    int button = 0;     ///< Mouse button index for Mouse* events
};

class IScene
{
public:
    virtual ~IScene() = default;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------
    virtual void onPush()   {}
    virtual void onPop()    {}   ///< Last call before destructor; free GPU resources here
    virtual void onPause()  {}   ///< Another scene pushed on top of this one
    virtual void onResume() {}   ///< The scene above this one was just popped

    // -------------------------------------------------------------------------
    // Per-frame
    // -------------------------------------------------------------------------

    /// Logic update.  @p dt is seconds since the last tick FOR THIS SCENE.
    /// SceneManager drives this at up to m_tick_hz times per second (0 = uncapped).
    virtual void tick(float dt, ClientViewState& view) {}

    /// Render.  Called every frame.  @p view is the ClientViewState as it was
    /// after the last tick() call.
    virtual void draw(const DrawContext& ctx, const ClientViewState& view) = 0;

    /// Handle an input event.  Only called on the top-of-stack scene.
    /// @return true if consumed (stop propagation), false to pass to scene below.
    virtual bool handleInput(const InputEvent& ev, ClientViewState& view) { return false; }

    /// Returns true if this scene owns and has drawn the zoom box for the
    /// current frame.  draw_zoom_box() in gui_parchment.c calls
    /// SceneManager_IsZoomBoxHandled() which uses this to skip its legacy body.
    virtual bool handlesZoomBox() const { return false; }

    // -------------------------------------------------------------------------
    // Tick-rate cap
    // -------------------------------------------------------------------------

    /// Maximum tick rate in Hz.  0 = uncapped (tick every frame).
    /// e.g. set to 15.f for a scene that only needs to refresh at 15fps.
    float tickHz() const { return m_tick_hz; }

protected:
    float m_tick_hz   = 0.f;
    float m_tick_accum = 0.f;   ///< Managed by SceneManager; do not write directly.

    friend class SceneManager;
};

#endif // __cplusplus
