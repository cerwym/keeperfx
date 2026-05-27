/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file SceneManager.h
 *     SceneManager — owns the scene stack and drives tick/draw/input.
 *
 *     Singleton.  Access via SceneManager::get().
 *
 *     The scene stack replaces the legacy FrontendMenuStates enum and the
 *     ad-hoc redraw_parchment_view / engine_redraw call sites.  During the
 *     migration period the legacy C paths are still called from within each
 *     scene; over time they will be replaced by scene-native logic.
 *
 *     ClientViewState lives here — one instance for the local player, never
 *     serialised, never transmitted.  syncViewFromLegacy() is called once per
 *     frame to mirror camera/zoom data from PlayerInfo until that data is fully
 *     migrated out of PlayerInfo.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "gui/IScene.h"
#include "gui/ClientViewState.h"
#include "gui/DrawContext.h"

#include <vector>
#include <memory>

class SceneManager
{
public:
    // -------------------------------------------------------------------------
    // Singleton access
    // -------------------------------------------------------------------------
    static SceneManager& get();

    // -------------------------------------------------------------------------
    // Stack operations
    // All take ownership via unique_ptr.  onPush() is called after push;
    // onPop() is called before the owning unique_ptr is reset.
    // -------------------------------------------------------------------------

    /// Push a new scene onto the stack.  Calls onPause() on the previous top,
    /// then onPush() on the new scene.
    void push(std::unique_ptr<IScene> scene);

    /// Pop the top scene.  Calls onPop() on it, then onResume() on the scene
    /// below (if any).
    void pop();

    /// Replace the top scene with a new one (pop + push as a single operation,
    /// without triggering onResume on the scene below mid-swap).
    void replace(std::unique_ptr<IScene> scene);

    /// Remove all scenes and push a new one.  Used for hard state transitions
    /// (e.g. entering gameplay from the main menu).
    void resetTo(std::unique_ptr<IScene> scene);

    /// True if the stack is empty (no scenes).
    bool empty() const;

    /// Number of scenes currently on the stack.
    int  depth() const;

    // -------------------------------------------------------------------------
    // Per-frame entry points — called from the C bridge (gui_bridge.cpp)
    // -------------------------------------------------------------------------

    /// Mirror legacy player view state into ClientViewState, then tick all
    /// scenes respecting their individual Hz caps.
    /// @param global_dt  Seconds since last frame (uncapped).
    void update(float global_dt);

    /// Capture DrawContext and draw all scenes bottom-to-top.
    void draw();

    /// Forward an input event to the top-of-stack scene only.
    /// @return true if consumed.
    bool handleInput(const InputEvent& ev);

    // -------------------------------------------------------------------------
    // View state access
    // -------------------------------------------------------------------------
    const ClientViewState& viewState() const { return m_view; }
    ClientViewState&       viewState()       { return m_view; }

    /// Non-owning pointer to the top-of-stack scene, or nullptr if empty.
    IScene* topScene() const { return m_stack.empty() ? nullptr : m_stack.back().get(); }

private:
    SceneManager() = default;
    ~SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    std::vector<std::unique_ptr<IScene>> m_stack;
    ClientViewState                       m_view;
};

#endif // __cplusplus
