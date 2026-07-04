/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file GameUI.h
 *     Owner/orchestrator of the composed in-game UI.
 * @par Purpose:
 *     Single home for "everything that is in-game 2D UI.": 
 * @par Comment:
 *     Intentionally a thin orchestrator in this pass -- it calls the
 *     existing draw_whole_status_panel()/draw_gui()/etc. C functions in the
 *     same order draw_2d_elements() used to. A future mod/event system can
 *     extend this surface (replace whole screens, add elements, hook game
 *     events) without inheriting the old layer-bleed bug class; that work
 *     is explicitly out of scope here.
 * @date     30 Jun 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_GAMEUI_H
#define KFX_GAMEUI_H

#include "bflib_basics.h"

struct PlayerInfo;

#ifdef __cplusplus

class GameUI {
public:
    static GameUI& Get();

    /** Composes the in-game sidebar for this frame by submitting several
     *  overlapping IR draws in order: the panel chrome, the room/spell/creature
     *  icons in the button slots, the minimap terrain and compass labels, plus
     *  in-game messages, the power-hand cursor, dialog boxes, and the
     *  pause/options overlay. Together these produce the complete sidebar the
     *  player sees — they are a single composed unit, not separate layers.
     *  Called once per frame from draw_2d_elements() after the 3D view and
     *  all world-space overlay sprites have already been submitted.
     *  No-ops if IsActiveForCurrentView(player) is false. */
    void DrawFrame(struct PlayerInfo* player);

    bool IsActiveForCurrentView(const struct PlayerInfo* player) const;

private:
    GameUI() = default;
    GameUI(const GameUI&) = delete;
    GameUI& operator=(const GameUI&) = delete;
};

#endif // __cplusplus

/* C-callable shim for engine_redraw.c. */
#ifdef __cplusplus
extern "C" {
#endif

void GameUI_DrawFrame(struct PlayerInfo* player);

#ifdef __cplusplus
}
#endif

/******************************************************************************/
#endif
