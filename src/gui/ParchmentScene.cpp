/******************************************************************************/
// KeeperFX — GUI Layer
/******************************************************************************/
/** @file ParchmentScene.cpp
 *     ParchmentScene implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "gui/ParchmentScene.h"

// Renderer
#include "renderer/RendererManager.h"   // RendererGetZoomBoxMode
#include "renderer/RendererSettings.h"  // ZBM_*

// Game
#include "player_data.h"        // get_my_player, PlayerInfo
#include "gui_parchment.h"      // load_parchment_file, draw_map_parchment, draw_2d_map,
                                //   draw_map_level_name
#include "gui_boxmenu.h"        // gui_draw_all_boxes
#include "gui_tooltips.h"       // draw_tooltip
#include "frontend.h"           // draw_gui
#include "bflib_video.h"        // pixel_size, scale_value_for_resolution
#include "bflib_planar.h"      // TbRect
#include "map_data.h"           // STL_PER_SLB

// ---------------------------------------------------------------------------
// Helpers — replicate the minimap_zoom → draw_tiles lookup from draw_zoom_box()
// ---------------------------------------------------------------------------

struct ZoomBoxParams {
    int draw_tiles;
    int subtile_unscaled;
};

static ZoomBoxParams zoomParamsFromMinimapZoom(unsigned short minimap_zoom)
{
    switch (minimap_zoom)
    {
    case 128:  return {  6, 18 };
    case 256:  return {  9, 12 };
    case 512:  return { 12,  9 };
    case 1024: return { 18,  6 };
    case 2048: return { 36,  3 };
    default:   return { 13,  8 };
    }
}

// ---------------------------------------------------------------------------

ParchmentScene::ParchmentScene()
{
    // Tick rate: uncapped (parchment map needs to track mouse every frame).
    m_tick_hz = 0.f;
}

void ParchmentScene::onPush()
{
    m_zoom_box = std::make_unique<ZoomBoxView>((int)RendererGetZoomBoxMode());
}

void ParchmentScene::onPop()
{
    m_zoom_box.reset();     // Destructs ZoomBoxView; FBO freed inside renderer
}

void ParchmentScene::tick(float /*dt*/, ClientViewState& /*view*/)
{
    // No per-tick logic yet — parchment view is purely reactive to mouse position.
}

void ParchmentScene::draw(const DrawContext& ctx, const ClientViewState& view)
{
    // Load parchment background (no-op if already loaded).
    load_parchment_file();
    // Draw the parchment background texture.
    draw_map_parchment();

    // -------------------------------------------------------------------------
    // Compute zoom box placement before draw_2d_map() so we can register the
    // screen rect with the renderer.  Overhead marker functions (draw_overhead_
    // creatures, draw_overhead_traps, etc.) query RendererPointInZoomBoxScreenRect
    // and skip any dot that falls inside the box, preventing bleed-through.
    // -------------------------------------------------------------------------
    TbRect  screen_rect   = {};
    int     stl_x         = 0;
    int     stl_y         = 0;
    int     draw_tiles    = 0;
    int     subtile_size  = 0;
    bool    has_zoom_box  = false;

    
    if (m_zoom_box)
    {
        const PlayerInfo* player = get_my_player();
        if (player && computeZoomBoxPlacement(ctx, view,
                                              &screen_rect, &stl_x, &stl_y,
                                              &draw_tiles, &subtile_size))
        {
            has_zoom_box = true;
            RendererSetZoomBoxScreenRect(screen_rect.left, screen_rect.top,
                                         screen_rect.right, screen_rect.bottom);
        }
    }

    // Draw the overhead 2D map tiles, creatures, rooms.
    // Creature dots that fall inside the zoom box rect are culled by the
    // renderer marker functions (via RendererPointInZoomBoxScreenRect).
    draw_2d_map();

    RendererClearZoomBoxScreenRect();

    // Draw all in-game GUI panels and menus.
    draw_gui();
    gui_draw_all_boxes();
    // Map level name and tooltip overlay.
    draw_map_level_name();
    draw_tooltip();

    // -------------------------------------------------------------------------
    // Zoom box render.
    // -------------------------------------------------------------------------
    if (!has_zoom_box)
        return;

    const PlayerInfo* player = get_my_player();
    if (!player)
        return;

    m_zoom_box->draw(ctx, view, player,
                     screen_rect,
                     stl_x, stl_y,
                     draw_tiles, draw_tiles,
                     subtile_size);
}

// ---------------------------------------------------------------------------
// Placement computation — all the mouse→tile math from draw_zoom_box(),
// reading from DrawContext instead of globals.
// ---------------------------------------------------------------------------
bool ParchmentScene::computeZoomBoxPlacement(const DrawContext&    ctx,
                                              const ClientViewState& view,
                                              TbRect*  out_screen_rect,
                                              int*     out_stl_x,
                                              int*     out_stl_y,
                                              int*     out_draw_tiles,
                                              int*     out_subtile_size) const
{
    ZoomBoxParams params = zoomParamsFromMinimapZoom(view.minimap_zoom);
    const int draw_tiles  = params.draw_tiles;
    const int subtile_size = scale_value_for_resolution(params.subtile_unscaled);

    // Compute the parchment background rect from screen dimensions.
    // Mirrors get_parchment_background_area_rect() / get_parchment_map_area_rect().
    const int img_w = (ctx.screen_w < 640) ? 320 : 640;
    const int img_h = (ctx.screen_w < 640) ? 200 : 480;
    int units_per_px = max(16 * ctx.screen_w / img_w, 16 * ctx.screen_h / img_h);
    const int units_per_px_max = min(16 * 7 * ctx.screen_w / (6 * img_w),
                                     16 * 4 * ctx.screen_h / (3 * img_h));
    if (units_per_px > units_per_px_max) units_per_px = units_per_px_max;
    if (units_per_px < 1)               units_per_px = 1;

    TbRect bkgnd;
    bkgnd.left   = (ctx.screen_w - units_per_px * img_w / 16) / 2;
    bkgnd.top    = (ctx.screen_h - units_per_px * img_h / 16) / 2;
    if (bkgnd.top < 0) bkgnd.top = 0;
    bkgnd.right  = bkgnd.left + units_per_px * img_w / 16;
    bkgnd.bottom = bkgnd.top  + units_per_px * img_h / 16;
    if (bkgnd.bottom > ctx.screen_h) bkgnd.bottom = ctx.screen_h;

    const long bkgnd_w = bkgnd.right  - bkgnd.left;
    const long bkgnd_h = bkgnd.bottom - bkgnd.top;
    long block_size = min((bkgnd_w - bkgnd_w / 3) / view.map_tiles_x,
                          (bkgnd_h - bkgnd_h / 8) / view.map_tiles_y);
    if (block_size < 1) block_size = 1;

    TbRect map_area;
    map_area.left   = bkgnd.left + (bkgnd_w - block_size * view.map_tiles_x) / 2;
    map_area.top    = bkgnd.top  + 3 * (bkgnd_h - block_size * view.map_tiles_y) / 4;
    map_area.right  = map_area.left + block_size * view.map_tiles_x;
    map_area.bottom = map_area.top  + block_size * view.map_tiles_y;

    // Screen position of the box: follow mouse with offset, clamp to screen.
    const long offset = scale_value_for_resolution(24);
    long scr_x = ctx.mouse_x + offset;
    long scr_y = ctx.mouse_y + offset;
    const long box_w = draw_tiles * subtile_size;
    const long box_h = draw_tiles * subtile_size;
    if (scr_x > ctx.screen_w - box_w)  scr_x = ctx.screen_w - box_w;
    if (scr_x < 0)                      scr_x = 0;
    if (scr_y > ctx.screen_h - box_h)  scr_y = ctx.screen_h - box_h;
    if (scr_y < 0)                      scr_y = 0;

    // Map tile under cursor.
    const int stl_x = STL_PER_SLB * (ctx.mouse_x / ctx.pixel_size - map_area.left)
                      / (int)block_size - draw_tiles / 2;
    const int stl_y = STL_PER_SLB * (ctx.mouse_y / ctx.pixel_size - map_area.top)
                      / (int)block_size - draw_tiles / 2;

    // Reject if cursor is outside the map area.
    const int map_subtiles_x = view.map_tiles_x * STL_PER_SLB;
    const int map_subtiles_y = view.map_tiles_y * STL_PER_SLB;
    if (stl_x < -draw_tiles / 2 || stl_x >= map_subtiles_x + 1 - draw_tiles / 2 ||
        stl_y < -draw_tiles / 2 || stl_y >= map_subtiles_y + 1 - draw_tiles / 2)
        return false;

    out_screen_rect->left   = (TbScreenCoord)scr_x;
    out_screen_rect->top    = (TbScreenCoord)scr_y;
    out_screen_rect->right  = (TbScreenCoord)(scr_x + box_w);
    out_screen_rect->bottom = (TbScreenCoord)(scr_y + box_h);
    *out_stl_x        = stl_x;
    *out_stl_y        = stl_y;
    *out_draw_tiles   = draw_tiles;
    *out_subtile_size = subtile_size;
    return true;
}
