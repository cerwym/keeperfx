/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiSpriteAtlasPanel.hpp
 *     ImGui debug viewer for the desktop-GL sprite atlas.
 *
 * @par Purpose:
 *     Runtime visualisation of every sprite currently packed into the live
 *     GLSpriteAtlas: a full-atlas view plus a per-sprite grid, with a
 *     materialiser mode toggle (indexed "as drawn" vs raw index vs the future
 *     truecolor path).  Read-only; snapshots the atlas under its shared lock.
 */
/******************************************************************************/
#ifndef IMGUI_SPRITE_ATLAS_PANEL_HPP
#define IMGUI_SPRITE_ATLAS_PANEL_HPP

#ifdef KEEPERFX_IMGUI_ENABLED

#ifdef __cplusplus
extern "C" {
#endif

/* Draw the sprite atlas viewer window if visible.
 * Call inside an ImGui frame (between NewFrame/Render) on the render thread. */
void ImGuiSpriteAtlasPanel_Draw(void);

/* Show/hide the viewer window (driven by the debug menu bar). */
void ImGuiSpriteAtlasPanel_SetVisible(int visible);
int  ImGuiSpriteAtlasPanel_IsVisible(void);

#ifdef __cplusplus
}
#endif

#else
#define ImGuiSpriteAtlasPanel_Draw()          ((void)0)
#define ImGuiSpriteAtlasPanel_SetVisible(v)   ((void)0)
#define ImGuiSpriteAtlasPanel_IsVisible()     0
#endif

#endif
