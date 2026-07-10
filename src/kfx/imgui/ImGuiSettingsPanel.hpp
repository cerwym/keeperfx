/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiSettingsPanel.hpp
 *     ImGui keeperfx.cfg settings panel.
 */
/******************************************************************************/
#ifndef IMGUI_SETTINGS_PANEL_HPP
#define IMGUI_SETTINGS_PANEL_HPP

#ifdef KEEPERFX_IMGUI_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the settings panel. Call inside an ImGui frame (between NewFrame/Render).
 * Returns 1 if the user clicked "Save Changes". */
int ImGuiSettingsPanel_Draw(void);

#ifdef __cplusplus
}
#endif

#else
#define ImGuiSettingsPanel_Draw() 0
#endif

#endif
