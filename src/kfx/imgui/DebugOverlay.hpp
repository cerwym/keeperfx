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

#ifdef __cplusplus
extern "C" {
#endif

void DebugOverlay_Initialize(void* sdl_window, void* sdl_gl_context);
void DebugOverlay_Shutdown(void);
void DebugOverlay_NewFrame(void);
void DebugOverlay_Render(void);
void DebugOverlay_QueueEvent(const void* sdl_event);
void DebugOverlay_Toggle(void);
int  DebugOverlay_IsVisible(void);
int  DebugOverlay_WantCaptureMouse(void);
int  DebugOverlay_WantCaptureKeyboard(void);

#ifdef __cplusplus
}
#endif

#else

#define DebugOverlay_Initialize(w, ctx)      ((void)0)
#define DebugOverlay_Shutdown()              ((void)0)
#define DebugOverlay_NewFrame()              ((void)0)
#define DebugOverlay_Render()                ((void)0)
#define DebugOverlay_QueueEvent(e)           ((void)0)
#define DebugOverlay_Toggle()                ((void)0)
#define DebugOverlay_IsVisible()             0
#define DebugOverlay_WantCaptureMouse()      0
#define DebugOverlay_WantCaptureKeyboard()   0

#endif

#endif /* DEBUG_OVERLAY_HPP */
