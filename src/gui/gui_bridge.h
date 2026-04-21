/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file gui_bridge.h
 *     C-visible entry points into the C++ GUI / SceneManager layer.
 *
 *     These are the ONLY functions the legacy C game-loop files need to call.
 *     During Phase 1 they simply forward to the legacy draw paths.
 *     As scenes are implemented they will progressively replace those calls.
 *
 *     Include this header from C or C++ files.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Called once per frame from the game-logic tick (gameplay_loop_logic /
/// frontend update path).  @p global_dt is seconds since the last frame.
void GUIBridge_Update(float global_dt);

/// Called once per frame from the draw path (gameplay_loop_draw /
/// frontend draw path), BEFORE RendererEndFrame.
void GUIBridge_Draw(void);

/// Returns non-zero when a scene on the stack has already drawn the zoom box
/// this frame, so draw_zoom_box() in gui_parchment.c should skip its body.
/// Phase 1: always returns 0.  Phase 3+: returns 1 when ParchmentScene active.
int SceneManager_IsZoomBoxHandled(void);

/// Called from draw_zoom_box() in gui_parchment.c.
/// Phase 2: if ParchmentScene is active on the stack it handles the zoom box
/// directly; this is a no-op in that case.  Until ParchmentScene is pushed,
/// this is also a no-op and the legacy draw_zoom_box() body runs instead.
void GUIBridge_DrawZoomBox(void);

/// Push ParchmentScene (if not already on top) and draw all scenes.
/// Replaces the direct redraw_parchment_view() call in engine_redraw.c for
/// the PVM_ParchmentView case.
void GUIBridge_DrawParchmentView(void);

/// Pop ParchmentScene from the stack (if it is the current top).
/// Call when the engine transitions away from PVM_ParchmentView.
void GUIBridge_LeaveParchmentView(void);

/// Called from the input handler with a key-down event.
/// @return non-zero if the event was consumed by the scene stack.
int GUIBridge_HandleKeyDown(int key);

/// Set the minimap zoom level directly (bypasses the packet queue).
/// Clamps to [128, 2048], updates player->minimap_zoom, settings, and saves.
void GUIBridge_SetMinimapZoom(unsigned short new_zoom);

/// Set the camera rotation angle directly (bypasses the packet queue).
/// Writes to all three cameras in PlayerInfo and calls set_local_camera_destination.
void GUIBridge_SetMapRotation(int angle);

/// Called from the input handler with a mouse-button-down event.
/// @return non-zero if the event was consumed by the scene stack.
int GUIBridge_HandleMouseDown(int x, int y, int button);

/// Toggle the game-view PiP zoom box on/off.
/// When turning on, snapshots cursor_subtile_x/y from the current player.
/// Shortcut: Ctrl+1.
void GUIBridge_ToggleGameViewPiP1(void);

/// Toggle a second game-view PiP zoom box on/off.
/// When turning on, snapshots cursor_subtile_x/y from the current player.
/// Shortcut: Ctrl+2.
void GUIBridge_ToggleGameViewPiP2(void);

/// Draw all active game-view PiP boxes stacked vertically in the top-right of
/// the engine window.  Called after redraw_isometric_view() in engine_redraw.c.
void GUIBridge_DrawGameViewPiP(void);

/** Handle mouse input over any active PiP window.
 *  Right-release drops a held creature/object; left-release navigates the main camera.
 *  Mirrors do_right_map_click / do_left_map_click for the minimap.
 *  Returns non-zero if the event was consumed (mouse was over a PiP). */
int  GUIBridge_HandleGameViewPiPInput(void);

/** Returns non-zero for the duration of a frame when the cursor is inside a PiP window.
 *  Used by draw_power_hand() to draw the keeper hand / mini-things at the cursor. */
int  GUIBridge_IsMouseOverPiP(void);

#ifdef __cplusplus
} // extern "C"
#endif
