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

#ifdef KEEPERFX_IMGUI_ENABLED
#include "kfx/imgui/DebugOverlay.hpp"
#include "kfx/imgui/CreatureSpriteCache.h"
#include "kfx/imgui/ImGuiCheatPanel.hpp"
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

bool DevTools::initOverlay(void* sdl_window, void* sdl_gl_context)
{
    return DebugOverlay_Initialize(sdl_window, sdl_gl_context) != 0;
}
void DevTools::shutdownOverlay()                 { DebugOverlay_Shutdown(); }
void DevTools::drawOverlay()                     { DebugOverlay_NewFrame(); DebugOverlay_Render(); }
void DevTools::queueEvent(const void* sdl_event) { DebugOverlay_QueueEvent(sdl_event); }
bool DevTools::isOverlayVisible() const          { return DebugOverlay_IsVisible() != 0; }
void DevTools::toggleOverlay()                   { DebugOverlay_Toggle(); }
bool DevTools::isCheatMenuVisible() const        { return ImGuiCheatPanel_IsVisible() != 0; }
void DevTools::toggleCheatMenu()                 { ImGuiCheatPanel_ToggleVisible(); }
void DevTools::closeCheatMenu()                  { ImGuiCheatPanel_SetVisible(0); }
void DevTools::setCheatMenuSection(int section)  { ImGuiCheatPanel_SetSection(section); }
bool DevTools::wantCaptureMouse() const          { return DebugOverlay_WantCaptureMouse() != 0; }
bool DevTools::wantCaptureKeyboard() const       { return DebugOverlay_WantCaptureKeyboard() != 0; }
void DevTools::serviceGameThread()               { CreatureSpriteCache_Service(); }
void DevTools::invalidateCreatureSprites()       { CreatureSpriteCache_Invalidate(); }

#else // ---- ImGui tooling not built: everything is a no-op ----

bool DevTools::initOverlay(void*, void*)         { return true; }
void DevTools::shutdownOverlay()                 {}
void DevTools::drawOverlay()                     {}
void DevTools::queueEvent(const void*)           {}
bool DevTools::isOverlayVisible() const          { return false; }
void DevTools::toggleOverlay()                   {}
bool DevTools::isCheatMenuVisible() const        { return false; }
void DevTools::toggleCheatMenu()                 {}
void DevTools::closeCheatMenu()                  {}
void DevTools::setCheatMenuSection(int)          {}
bool DevTools::wantCaptureMouse() const          { return false; }
bool DevTools::wantCaptureKeyboard() const       { return false; }
void DevTools::serviceGameThread()               {}
void DevTools::invalidateCreatureSprites()       {}

#endif

} // namespace kfx

/******************************************************************************/
// C shim — for plain C translation units.

extern "C" int KfxDevTools_Available(void)
{
    return kfx::DevTools::available() ? 1 : 0;
}
extern "C" int KfxDevTools_InitOverlay(void* sdl_window, void* sdl_gl_context)
{
    return kfx::DevTools::instance().initOverlay(sdl_window, sdl_gl_context) ? 1 : 0;
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
extern "C" int KfxDevTools_IsCheatMenuVisible(void)
{
    return kfx::DevTools::instance().isCheatMenuVisible() ? 1 : 0;
}
extern "C" void KfxDevTools_ToggleCheatMenu(void)
{
    kfx::DevTools::instance().toggleCheatMenu();
}
extern "C" void KfxDevTools_CloseCheatMenu(void)
{
    kfx::DevTools::instance().closeCheatMenu();
}
extern "C" void KfxDevTools_SetCheatMenuSection(int section)
{
    kfx::DevTools::instance().setCheatMenuSection(section);
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
extern "C" void KfxDevTools_InvalidateCreatureSprites(void)
{
    kfx::DevTools::instance().invalidateCreatureSprites();
}
/******************************************************************************/
