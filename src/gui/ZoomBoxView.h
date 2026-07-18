/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ZoomBoxView.h
 *     ZoomBoxView — re-usable zoom / PiP box leaf view.
 *
 *     Renders a rectangular inset view of the world at a given screen rect.
 *     Supports two modes:
 *       ZBM_OVERHEAD   — flat top-down terrain + thing sprites (legacy path).
 *       ZBM_ISOMETRIC  — full isometric 3D render via GPU FBO PiP.
 *
 *     The view owns NO position logic.  The caller scene computes the screen
 *     rect (origin + size) and the centre map tile, then passes them in.
 *     This makes ZoomBoxView reusable for:
 *       - Parchment hover box  (ParchmentScene, rect follows mouse)
 *       - Battle-tracker box  (future BattleTrackerSubScene, tracks combat tile)
 *
 *     Corner frame sprites are drawn by this view (they are intrinsic to the
 *     box visual, not to any scene-level policy).
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "gui/IView.h"
#include "engine_camera.h"  // Camera, CAMERA_ZOOM_MIN/MAX, CamIV_*
#include "bflib_planar.h"  // TbRect

class ZoomBoxView : public IView
{
public:
    /// @param mode  ZBM_OVERHEAD or ZBM_ISOMETRIC.
    explicit ZoomBoxView(int mode);

    // IView
    void draw(const DrawContext&    ctx,
              const ClientViewState& view,
              const PlayerInfo*     player) override;

    /// Extended draw called by the owning scene when it has already computed
    /// the placement.
    ///
    /// @param ctx          Per-frame screen/input snapshot.
    /// @param view         Local view state (cameras, minimap_zoom).
    /// @param player       Non-owning game-state player pointer.
    /// @param screen_rect  Screen pixel rect where the box should appear
    ///                     (top-left origin, width × height).
    /// @param stl_x        Subtile X of the top-left corner of the viewed area.
    /// @param stl_y        Subtile Y of the top-left corner of the viewed area.
    /// @param draw_tiles_x Number of tiles shown horizontally.
    /// @param draw_tiles_y Number of tiles shown vertically.
    /// @param subtile_size Pixel size of one subtile (scaled for resolution).
    void draw(const DrawContext&    ctx,
              const ClientViewState& view,
              const PlayerInfo*     player,
              TbRect                screen_rect,
              int                   stl_x,
              int                   stl_y,
              int                   draw_tiles_x,
              int                   draw_tiles_y,
              int                   subtile_size);

private:
    int  m_mode;              ///< ZoomBoxMode value (ZBM_OVERHEAD / ZBM_ISOMETRIC)

    void drawIsometric(const DrawContext& ctx, const ClientViewState& view,
                       const PlayerInfo* player,
                       int scr_x, int scr_y,
                       int stl_x,   int stl_y,
                       int draw_tiles_x, int draw_tiles_y, int subtile_size);

    void drawOverhead(const DrawContext& ctx, const PlayerInfo* player,
                      int scr_x, int scr_y,
                      int stl_x,   int stl_y,
                      int draw_tiles_x, int draw_tiles_y, int subtile_size);

    void drawCornerFrames(int scr_x, int scr_y,
                          int draw_tiles_x, int draw_tiles_y,
                          int subtile_size, int units_per_pixel) const;
};

#endif // __cplusplus
