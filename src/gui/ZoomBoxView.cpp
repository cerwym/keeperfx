/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ZoomBoxView.cpp
 *     ZoomBoxView implementation.
 *     Logic extracted from draw_zoom_box() in gui_parchment.c.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/ZoomBoxView.h"
#include "gui/DrawFlagsGuard.h"

// Renderer abstraction
#include "renderer/RendererManager.h"   // RendererSchedulePiPRender, RendererGetZoomBoxMode,
                                        // RendererGetActiveType, UIRenderer_SubmitButtonSprite
#include "renderer/RendererSettings.h"  // ZoomBoxMode, ZBM_ISOMETRIC, ZBM_OVERHEAD

// Game headers
#include "player_data.h"    // PlayerInfo
#include "engine_camera.h"  // Camera, get_camera_zoom, set_camera_zoom, CAMERA_ZOOM_MIN/MAX
#include "bflib_video.h"    // MyScreenWidth/Height, pixel_size, units_per_pixel
#include "bflib_vidraw.h"   // vec_window_height
#include "bflib_planar.h"  // TbRect
#include "bflib_sprite.h"   // TbSprite (SWidth/SHeight fields)
#include "custom_sprites.h" // get_button_sprite
#include "sprites.h"        // GBS_parchment_map_frame_deco_b_*
#include "gui_parchment.h"  // draw_zoom_box_terrain, draw_zoom_box_things (legacy fallback)

// Forward declarations for legacy C functions not in their header yet
extern "C" {
    void draw_zoom_box_terrain(long scrtop_x, long scrtop_y, int stl_x, int stl_y,
                               PlayerNumber plyr_idx, long draw_tiles_x, long draw_tiles_y,
                               int subtile_size);
    void draw_zoom_box_things(long scrtop_x, long scrtop_y, int stl_x, int stl_y,
                              PlayerNumber plyr_idx, long draw_tiles_x, long draw_tiles_y,
                              int subtile_size);
}

// ---------------------------------------------------------------------------

ZoomBoxView::ZoomBoxView(int mode)
    : m_mode(mode), m_fixed_zoom(0), m_fixed_rotation(-1)
{}

// ---------------------------------------------------------------------------
// IView::draw — called when the scene provides no pre-computed rect.
// Delegates to the extended overload with a zero rect; scenes that rely on
// this path should use the extended overload directly.
// ---------------------------------------------------------------------------
void ZoomBoxView::draw(const DrawContext&     /*ctx*/,
                       const ClientViewState& /*view*/,
                       const PlayerInfo*      /*player*/)
{
    // No-op: ZoomBoxView requires an explicit screen_rect + tile coordinates.
    // Use the extended draw() overload from the owning scene.
}

// ---------------------------------------------------------------------------
// Extended draw — called by ParchmentScene (and future scenes) with all
// placement already computed.
// ---------------------------------------------------------------------------
void ZoomBoxView::draw(const DrawContext&    ctx,
                       const ClientViewState& view,
                       const PlayerInfo*     player,
                       TbRect                screen_rect,
                       int                   stl_x,
                       int                   stl_y,
                       int                   draw_tiles_x,
                       int                   draw_tiles_y,
                       int                   subtile_size)
{
    // Tuneable offsets: shift the isometric PiP content independently of the
    // ornate corner-frame sprites.  The overhead path has its own built-in
    // offset in draw_zoom_box_terrain() so it uses the raw screen_rect position.
    const int content_offset_x = 14;
    const int content_offset_y = -14;

    const int scr_x = screen_rect.left;
    const int scr_y = screen_rect.top;

    // Corner radius for the rounded-rect content mask (base-resolution pixels).
    // Approximates the ornate frame's inner contour so content doesn't poke
    // out beyond the decorative corners.  Tune visually.
    const int base_clip_radius = 14;
    const float clip_radius = (float)((base_clip_radius * ctx.units_per_pixel + 8) / 16);
    RendererSetZoomBoxClipRadius(clip_radius);

    if (m_mode == ZBM_ISOMETRIC && RendererHasGPURenderPath())
        drawIsometric(ctx, view, player,
                      scr_x + content_offset_x, scr_y + content_offset_y,
                      stl_x, stl_y, draw_tiles_x, draw_tiles_y, subtile_size);
    else
        drawOverhead(ctx, player, scr_x, scr_y,
                     stl_x, stl_y, draw_tiles_x, draw_tiles_y, subtile_size);

    RendererSetZoomBoxClipRadius(-1.0f);

    // Corner frames use the original unshifted position.
    drawCornerFrames(screen_rect.left, screen_rect.top, draw_tiles_x, draw_tiles_y,
                     subtile_size, ctx.units_per_pixel);

    // Restore the full-screen graphics window (legacy C draw paths may clip it).
    RendererSetViewport(0, 0,
                        ctx.screen_w / ctx.pixel_size,
                        ctx.screen_h / ctx.pixel_size);
}

// ---------------------------------------------------------------------------
// Isometric PiP path
// ---------------------------------------------------------------------------
void ZoomBoxView::drawIsometric(const DrawContext&    ctx,
                                const ClientViewState& view,
                                const PlayerInfo*     player,
                                int scr_x, int scr_y,
                                int stl_x,   int stl_y,
                                int draw_tiles_x, int draw_tiles_y,
                                int subtile_size)
{
    // Build a pip camera centred on the tile under the cursor.
    // Base camera is the isometric view camera from ClientViewState —
    // we use the local copy so we never mutate PlayerInfo.
    Camera pip_cam = view.cameras[CamIV_Isometric];

    const int stl_cx = stl_x + draw_tiles_x / 2;
    const int stl_cy = stl_y + draw_tiles_y / 2;
    pip_cam.mappos.x.val = stl_cx * 256 + 128;
    pip_cam.mappos.y.val = stl_cy * 256 + 128;

    // Apply fixed rotation if set, so PiP orientation is independent of main camera.
    if (m_fixed_rotation >= 0)
        pip_cam.rotation_angle_x = (long)m_fixed_rotation;

    const int pip_w = draw_tiles_x * subtile_size;
    const int pip_h = draw_tiles_y * subtile_size;

    // Scale zoom so tile density is proportional to the pip viewport height
    // relative to the main window height.  If a fixed zoom was set, use it
    // directly so the PiP is independent of the main camera zoom.
    {
        const long base_zoom = (m_fixed_zoom > 0)
            ? m_fixed_zoom
            : get_camera_zoom(const_cast<Camera*>(&view.cameras[CamIV_Isometric]));
        const long ref_h = (long)vec_window_height;
        long scaled_zoom = (ref_h > 0)
            ? base_zoom * 13 * pip_h / (draw_tiles_x * ref_h)
            : base_zoom * 13 / draw_tiles_x;
        if (scaled_zoom > CAMERA_ZOOM_MAX) scaled_zoom = CAMERA_ZOOM_MAX;
        if (scaled_zoom < CAMERA_ZOOM_MIN) scaled_zoom = CAMERA_ZOOM_MIN;
        set_camera_zoom(&pip_cam, scaled_zoom);
    }

    RendererSchedulePiPRender(&pip_cam, scr_x, scr_y, pip_w, pip_h);
}

// ---------------------------------------------------------------------------
// Overhead (flat) path — delegates to legacy C functions
// ---------------------------------------------------------------------------
void ZoomBoxView::drawOverhead(const DrawContext& /*ctx*/,
                               const PlayerInfo*  player,
                               int scr_x, int scr_y,
                               int stl_x,   int stl_y,
                               int draw_tiles_x, int draw_tiles_y,
                               int subtile_size)
{
    draw_zoom_box_terrain(scr_x, scr_y, stl_x, stl_y,
                          player->id_number,
                          draw_tiles_x, draw_tiles_y, subtile_size);
    draw_zoom_box_things(scr_x, scr_y, stl_x, stl_y,
                         player->id_number,
                         draw_tiles_x, draw_tiles_y, subtile_size);
}

// ---------------------------------------------------------------------------
// Corner frame sprites — intrinsic to the box visual
// ---------------------------------------------------------------------------
void ZoomBoxView::drawCornerFrames(int scr_x, int scr_y,
                                   int draw_tiles_x, int draw_tiles_y,
                                   int subtile_size, int units_per_px) const
{
    const struct TbSprite* spr = get_button_sprite(GBS_parchment_map_frame_deco_b_tl);
    if (!spr)
        return;
    const int bs_units_per_px = (74 * units_per_px) / spr->SWidth;

    // Offsets match the original draw_zoom_box() exactly.
    const int scale_20 = (20 * units_per_px + 8) / 16;   // scale_value_for_resolution(20)
    const int scale_24 = (24 * units_per_px + 8) / 16;
    const int scale_46 = (46 * units_per_px + 8) / 16;
    const int scale_58 = (58 * units_per_px + 8) / 16;

    const int beg_x = scr_x - scale_20;
    const int beg_y = scr_y - scale_24;
    const int end_x = scr_x - scale_46 + draw_tiles_x * subtile_size;
    const int end_y = scr_y - scale_58 + draw_tiles_y * subtile_size;

    UIRenderer_SubmitButtonSprite(beg_x, beg_y, bs_units_per_px, GBS_parchment_map_frame_deco_b_tl);
    UIRenderer_SubmitButtonSprite(end_x, beg_y, bs_units_per_px, GBS_parchment_map_frame_deco_b_tr);
    UIRenderer_SubmitButtonSprite(beg_x, end_y, bs_units_per_px, GBS_parchment_map_frame_deco_b_bl);
    UIRenderer_SubmitButtonSprite(end_x, end_y, bs_units_per_px, GBS_parchment_map_frame_deco_b_br);
}
