/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareLensRenderer.cpp
 *     CPU software implementation of ILensRenderer.
 * @par Purpose:
 *     Owns the software backend's immediate-mode world-capture bracket. This
 *     logic previously lived on RendererSoftware (BeginLensCapture/EndLensCapture);
 *     it now lives here so each backend fully owns its lens realization.
 *
 *     Runs entirely on the game thread (which is the render thread for the
 *     software backend), mid-frame, around engine()+swipe in draw_creature_view().
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareLensRenderer.h"

#include "renderer/RendererManager.h"
#include "engine_redraw.h"   // setup_engine_window
#include "lens_api.h"        // lens_is_ready / lens_get_render_target* / draw_lens_effect
#include "player_data.h"     // get_my_player, PlayerInfo
#include "game_legacy.h"     // game.applied_lens_type

#include <cstring>

#include "post_inc.h"

/******************************************************************************/

bool SoftwareLensRenderer::BeginWorldCapture()
{
    m_capture_active = false;
    if (!lens_is_ready())
        return false;

    m_lens_buffer   = lens_get_render_target();
    m_lens_buffer_w = lens_get_render_target_width();
    m_lens_buffer_h = lens_get_render_target_height();
    if (!m_lens_buffer || m_lens_buffer_w == 0 || m_lens_buffer_h == 0)
        return false;

    setup_engine_window(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());

    // Snapshot the distort params now: later UI draws in redraw_creature_view may
    // change the engine window before EndFrame.
    struct PlayerInfo* player = get_my_player();
    m_view_width  = player->engine_window_width / pixel_size;
    m_view_height = player->engine_window_height / pixel_size;
    m_view_x      = player->engine_window_x / pixel_size;
    m_view_y      = player->engine_window_y / pixel_size;
    m_lens_type   = game.applied_lens_type;

    m_capture_active = true;
    return true;
}

TbGraphicsWindow SoftwareLensRenderer::ResolveWorldCaptureBegin(const TbGraphicsWindow& screen_target)
{
    if (!m_capture_active)
        return screen_target;

    // Hand back the off-screen lens buffer as an explicit draw target for the
    // deferred world (the world executor rasterises into it via setup_vecs).
    memset(m_lens_buffer, 0, (size_t)m_lens_buffer_w * (size_t)m_lens_buffer_h * sizeof(TbPixel));

    TbGraphicsWindow lens_target;
    lens_target.x             = 0;
    lens_target.y             = 0;
    lens_target.width         = (long)m_lens_buffer_w;
    lens_target.height        = (long)m_lens_buffer_h;
    lens_target.ptr           = m_lens_buffer;
    lens_target.scanline      = (long)m_lens_buffer_w;
    lens_target.screen_height = (long)m_lens_buffer_h;

    // The caller wraps the world execution in a SwTargetScope(lens_target) so the
    // legacy bflib_vidraw.* sub-draws (e.g. the possession flame) rasterise into
    // this buffer too — no ambient renderer-state mutation here.
    return lens_target;
}

void SoftwareLensRenderer::ResolveWorldCaptureEnd(const TbGraphicsWindow& screen_target)
{
    if (!m_capture_active)
        return;

    const long dst_offset = m_view_y * screen_target.scanline + m_view_x;
    draw_lens_effect(screen_target.ptr + dst_offset, screen_target.scanline,
        m_lens_buffer, m_lens_buffer_w, m_view_width, m_view_height, m_view_x, m_lens_type);

    m_capture_active = false;
    m_lens_buffer    = nullptr;
    m_lens_buffer_w  = 0;
    m_lens_buffer_h  = 0;
}

/******************************************************************************/
