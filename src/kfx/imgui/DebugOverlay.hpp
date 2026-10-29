/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DebugOverlay.hpp
 *     ImGui debug overlay shell API.
 */
/******************************************************************************/
#ifndef DEBUG_OVERLAY_HPP
#define DEBUG_OVERLAY_HPP

#ifdef KEEPERFX_IMGUI_ENABLED

#include <memory>

namespace kfx { class IImGuiRenderBackend; }

int  DebugOverlay_Initialize(std::unique_ptr<kfx::IImGuiRenderBackend> backend);
void DebugOverlay_Shutdown(void);
void DebugOverlay_NewFrame(void);
void DebugOverlay_Render(void);
void DebugOverlay_QueueEvent(const void* sdl_event);
void DebugOverlay_Toggle(void);
int  DebugOverlay_IsVisible(void);
int  DebugOverlay_WantCaptureMouse(void);
int  DebugOverlay_WantCaptureKeyboard(void);

#endif

#endif /* DEBUG_OVERLAY_HPP */
