/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file IView.h
 *     IView — leaf drawable interface.
 *
 *     A view is a self-contained piece of rendering logic that takes a
 *     DrawContext, a ClientViewState, and a PlayerInfo* (non-owning) and
 *     produces draw calls via RendererGetUIRenderer() / RendererSchedulePiPRender().
 *
 *     Views do NOT own game state.  They receive everything they need via
 *     arguments.  Their lifetime is controlled by the scene that owns them.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "gui/DrawContext.h"
#include "gui/ClientViewState.h"

struct PlayerInfo;

class IView
{
public:
    virtual ~IView() = default;

    /// Draw this view.  Called every renderer frame.
    /// @param ctx        Immutable per-frame screen/input snapshot.
    /// @param view       Local view state (cameras, minimap zoom, etc.).
    /// @param player     Non-owning pointer to the authoritative game-state player.
    virtual void draw(const DrawContext&    ctx,
                      const ClientViewState& view,
                      const PlayerInfo*     player) = 0;
};

#endif // __cplusplus
