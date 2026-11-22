/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DevTools.h
 *     Facade for the optional ImGui-based developer/debug tools.
 *
 *     This is the single entry point the rest of the engine talks to. Every
 *     method is ALWAYS available and safe to call unconditionally: when the
 *     build does not include ImGui (KEEPERFX_IMGUI_ENABLED undefined) every
 *     call is a cheap no-op. Because of that, call sites never need their own
 *     `#ifdef KEEPERFX_IMGUI_ENABLED` guards -- the one and only conditional
 *     compilation lives inside DevTools.cpp.
 *
 *     C++ code should prefer the object form `kfx::DevTools::instance()`; plain
 *     C translation units use the matching `KfxDevTools_*` C functions below.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_IMGUI_DEVTOOLS_H
#define KEEPERFX_KFX_IMGUI_DEVTOOLS_H

#ifdef __cplusplus

#include <memory>

namespace kfx {

class IImGuiRenderBackend;

/** Singleton facade over the developer tools (ImGui overlay + debug caches). */
class DevTools {
public:
    static DevTools& instance();

    /** True only when the binary was built with the ImGui tooling. Compile-time
     *  constant, so guarded blocks fold away when the tooling is absent. */
    static constexpr bool available()
    {
#ifdef KEEPERFX_IMGUI_ENABLED
        return true;
#else
        return false;
#endif
    }

    // ---- Overlay lifecycle (render thread; owns the render backend) ----
    /** Initialise the overlay with the given renderer-specific backend.
     *  Takes ownership of `backend` (deleted by shutdownOverlay()). Returns
     *  true on success (or when unavailable, so the caller's init sequence
     *  doesn't need its own branch). */
    bool initOverlay(std::unique_ptr<IImGuiRenderBackend> backend);
    void shutdownOverlay();
    /** Advance one ImGui frame and render the overlay (NewFrame + panels). */
    void drawOverlay();

    // ---- Input (render/input thread) ----
    void queueEvent(const void* sdl_event);
    bool isOverlayVisible() const;
    void toggleOverlay();
    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

    // ---- Game-thread services ----
    /** Pump per-frame debug work that must run on the game thread (safe file I/O
     *  and read-only config access). Call once per game-thread frame. */
    void serviceGameThread();

private:
    DevTools() = default;
};

} // namespace kfx

extern "C" {
#endif /* __cplusplus */

int  KfxDevTools_Available(void);
void KfxDevTools_ShutdownOverlay(void);
void KfxDevTools_DrawOverlay(void);
void KfxDevTools_QueueEvent(const void* sdl_event);
int  KfxDevTools_IsOverlayVisible(void);
void KfxDevTools_ToggleOverlay(void);
int  KfxDevTools_WantCaptureMouse(void);
int  KfxDevTools_WantCaptureKeyboard(void);
void KfxDevTools_ServiceGameThread(void);

#ifdef __cplusplus
}
#endif

#endif /* KEEPERFX_KFX_IMGUI_DEVTOOLS_H */
