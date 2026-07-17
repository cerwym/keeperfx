/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiCheatPanel.hpp
 *     ImGui in-game cheat menu panel.
 */
/******************************************************************************/
#ifndef IMGUI_CHEAT_PANEL_HPP
#define IMGUI_CHEAT_PANEL_HPP

#ifdef KEEPERFX_IMGUI_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

void ImGuiCheatPanel_Draw(void);
void ImGuiCheatPanel_SetVisible(int visible);
void ImGuiCheatPanel_ToggleVisible(void);
int  ImGuiCheatPanel_IsVisible(void);
void ImGuiCheatPanel_SetSection(int section);

#ifdef __cplusplus
}
#endif

#else

#define ImGuiCheatPanel_Draw()              ((void)0)
#define ImGuiCheatPanel_SetVisible(v)       ((void)0)
#define ImGuiCheatPanel_ToggleVisible()     ((void)0)
#define ImGuiCheatPanel_IsVisible()         0
#define ImGuiCheatPanel_SetSection(section) ((void)0)

#endif

#endif

