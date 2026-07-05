/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ParchmentScene.h
 *     ParchmentScene — the overhead parchment map view.
 *
 *     Owns a ZoomBoxView instance for the hover zoom box.  The scene computes
 *     the screen rect and tile coordinates from mouse position + minimap zoom
 *     and passes them down to ZoomBoxView::draw().
 *
 *     All other parchment drawing (background, 2D map tiles, GUI) still
 *     delegates to the legacy C functions during Phase 2.  Those will be
 *     progressively replaced in Phase 3.
 *
 *     Lifecycle:
 *       onPush()  — allocates ZoomBoxView with current ZoomBoxMode.
 *       onPop()   — ZoomBoxView destroyed (unique_ptr reset), FBO freed.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus

#include "gui/IScene.h"
#include "gui/ZoomBoxView.h"

#include <memory>

class ParchmentScene : public IScene
{
public:
    ParchmentScene();
    ~ParchmentScene() override = default;

    // IScene lifecycle
    void onPush()  override;
    void onPop()   override;

    // IScene per-frame
    void tick(float dt, ClientViewState& view) override;
    void draw(const DrawContext& ctx, const ClientViewState& view) override;

    bool handlesZoomBox() const override { return true; }
    bool wantsGameUI() const override { return false; }

private:
    std::unique_ptr<ZoomBoxView> m_zoom_box;

    /// Compute placement from mouse position and minimap_zoom.
    /// Returns false if the cursor is outside the map area (no box to draw).
    bool computeZoomBoxPlacement(const DrawContext&    ctx,
                                 const ClientViewState& view,
                                 TbRect*  out_screen_rect,
                                 int*     out_stl_x,
                                 int*     out_stl_y,
                                 int*     out_draw_tiles,
                                 int*     out_subtile_size) const;
};

#endif // __cplusplus
