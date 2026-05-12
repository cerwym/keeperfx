/******************************************************************************/
// KeeperFX — Level lifecycle management.
/******************************************************************************/
/** @file kfx/save/level_session.h
 *     LevelSession: named, independently-callable reinitialisation phases for
 *     the post-load / new-level lifecycle.
 *
 *     The existing reinit_level_after_load() delegates here; future callers
 *     (LevelSession::LoadFromSave, LevelSession::StartNew) will compose the
 *     phases directly, replacing the monolithic flat function.
 *
 *     Design rules:
 *      - C-callable (extern "C") — the reinit path is still C code.
 *      - Each phase is a free function; the "class" grouping is conceptual
 *        (namespace via prefix) until a full C++ migration is warranted.
 *      - No phase creates menus.  Menus are created lazily by turn_on_menu().
 */
/******************************************************************************/
#ifndef KFX_SAVE_LEVEL_SESSION_H
#define KFX_SAVE_LEVEL_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* Reinit phases — each covers one logical concern.                           */
/* Call in the order listed for a correct post-load state.                    */
/******************************************************************************/

/**
 * Phase 1 – palette, lookups, navigation, packets.
 * Must run first: downstream phases assume lookups are valid.
 */
void LevelSession_ReinitGameState(void);

/**
 * Phase 2 – texture map file and animating texture maps.
 * Notifies the renderer to discard its tile atlas so it is rebuilt from the
 * freshly loaded block_mem data on the next frame.
 */
void LevelSession_ReinitMapData(void);

/**
 * Phase 3 – UI slot reset and player-role configuration.
 * Only executes when RendererNeedsUIReinitAfterLoad() returns non-zero.
 * reset_gui_based_on_player_mode() must be called by the caller afterwards
 * to open the correct initial panels via the lazy turn_on_menu() path.
 */
void LevelSession_ReinitUI(void);

/**
 * Phase 4 – player view modes, panel colours, tagged blocks, erstats.
 */
void LevelSession_ReinitPlayerState(void);

/**
 * Phase 5 – AI (computer player) state restore.
 */
void LevelSession_ReinitAI(void);

/**
 * Phase 6 – audio reinitialisation and panel colour update.
 */
void LevelSession_ReinitAudio(void);

/**
 * Convenience: run all six phases in the correct order, then call
 * reset_gui_based_on_player_mode().  Equivalent to the previous
 * reinit_level_after_load() body but with named, auditable phases.
 */
void LevelSession_ReinitAfterLoad(void);

#ifdef __cplusplus
}
#endif

#endif /* KFX_SAVE_LEVEL_SESSION_H */
