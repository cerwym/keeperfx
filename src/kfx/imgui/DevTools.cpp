/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DevTools.cpp
 *     Developer-tools facade implementation.
 *
 *     This translation unit is ALWAYS compiled and linked, regardless of the
 *     KEEPERFX_IMGUI setting, so that the rest of the engine can call the
 *     DevTools API unconditionally. It is the single place that switches
 *     between the real ImGui-backed implementation and cheap no-ops.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/imgui/DevTools.h"
// std::unique_ptr<IImGuiRenderBackend>'s destructor needs the complete type
// wherever initOverlay() is DEFINED (both branches below, including the
// no-op one) -- DevTools.h only forward-declares it, correctly, since it
// only DECLARES initOverlay(). This header has no ImGui/SDL/GL content of
// its own, so including it here doesn't reintroduce what DevTools.cpp is
// trying to stay free of.
#include "kfx/imgui/IImGuiRenderBackend.h"

#ifdef KEEPERFX_IMGUI_ENABLED
#include "kfx/imgui/DebugOverlay.hpp"
#endif

#include "post_inc.h"
/******************************************************************************/

namespace kfx {

DevTools& DevTools::instance()
{
    static DevTools s_instance;
    return s_instance;
}

#ifdef KEEPERFX_IMGUI_ENABLED

bool DevTools::initOverlay(std::unique_ptr<IImGuiRenderBackend> backend)
{
    return DebugOverlay_Initialize(std::move(backend)) != 0;
}
void DevTools::shutdownOverlay()                 { DebugOverlay_Shutdown(); }
void DevTools::drawOverlay()                     { DebugOverlay_NewFrame(); DebugOverlay_Render(); }
void DevTools::queueEvent(const void* sdl_event) { DebugOverlay_QueueEvent(sdl_event); }
bool DevTools::isOverlayVisible() const          { return DebugOverlay_IsVisible() != 0; }
void DevTools::toggleOverlay()                   { DebugOverlay_Toggle(); }
bool DevTools::wantCaptureMouse() const          { return DebugOverlay_WantCaptureMouse() != 0; }
bool DevTools::wantCaptureKeyboard() const       { return DebugOverlay_WantCaptureKeyboard() != 0; }
void DevTools::serviceGameThread()               {}

#else // ---- ImGui tooling not built: everything is a no-op ----

bool DevTools::initOverlay(std::unique_ptr<IImGuiRenderBackend>) { return true; }
void DevTools::shutdownOverlay()                 {}
void DevTools::drawOverlay()                     {}
void DevTools::queueEvent(const void*)           {}
bool DevTools::isOverlayVisible() const          { return false; }
void DevTools::toggleOverlay()                   {}
bool DevTools::wantCaptureMouse() const          { return false; }
bool DevTools::wantCaptureKeyboard() const       { return false; }
void DevTools::serviceGameThread()               {}

#endif

} // namespace kfx

/******************************************************************************/
// C shim -- for plain C translation units.

extern "C" int KfxDevTools_Available(void)
{
    return kfx::DevTools::available() ? 1 : 0;
}
extern "C" void KfxDevTools_ShutdownOverlay(void)
{
    kfx::DevTools::instance().shutdownOverlay();
}
extern "C" void KfxDevTools_DrawOverlay(void)
{
    kfx::DevTools::instance().drawOverlay();
}
extern "C" void KfxDevTools_QueueEvent(const void* sdl_event)
{
    kfx::DevTools::instance().queueEvent(sdl_event);
}
extern "C" int KfxDevTools_IsOverlayVisible(void)
{
    return kfx::DevTools::instance().isOverlayVisible() ? 1 : 0;
}
extern "C" void KfxDevTools_ToggleOverlay(void)
{
    kfx::DevTools::instance().toggleOverlay();
}
extern "C" int KfxDevTools_WantCaptureMouse(void)
{
    return kfx::DevTools::instance().wantCaptureMouse() ? 1 : 0;
}
extern "C" int KfxDevTools_WantCaptureKeyboard(void)
{
    return kfx::DevTools::instance().wantCaptureKeyboard() ? 1 : 0;
}
extern "C" void KfxDevTools_ServiceGameThread(void)
{
    kfx::DevTools::instance().serviceGameThread();
}
/******************************************************************************/
