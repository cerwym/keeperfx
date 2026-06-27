/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiRendererPanel.hpp
 *     ImGui renderer and window settings panel.
 */
/******************************************************************************/
#ifndef IMGUI_RENDERER_PANEL_HPP
#define IMGUI_RENDERER_PANEL_HPP

#ifdef KEEPERFX_IMGUI_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the renderer/window settings panel.
 * Call inside an ImGui frame (between NewFrame/Render).
 * Returns 1 if settings were applied and saved. */
int ImGuiRendererPanel_Draw(void);

#ifdef __cplusplus
}
#endif

#else
#define ImGuiRendererPanel_Draw() 0
#endif

#endif
