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

    // Legacy bflib_vidraw.* world sub-draws (e.g. the possession flame effect)
    // still read the *ambient* CPU target rather than the setup_vecs target, so
    // point the ambient target at the lens buffer too — otherwise they draw to
    // the screen and are overwritten by the distort below.  Phase C2 replaces
    // this ambient set with an explicit scope guard around those primitives.
    RendererSetWScreen(m_lens_buffer);
    RendererSetScreenDimensions((int)m_lens_buffer_w, (int)m_lens_buffer_h);

    return lens_target;
}

void SoftwareLensRenderer::ResolveWorldCaptureEnd(const TbGraphicsWindow& screen_target)
{
    if (!m_capture_active)
        return;

    // Restore the ambient CPU target to the on-screen surface before distorting.
    RendererSetWScreen(screen_target.ptr);
    RendererSetScreenDimensions((int)screen_target.scanline, (int)screen_target.screen_height);

    const long dst_offset = m_view_y * screen_target.scanline + m_view_x;
    draw_lens_effect(screen_target.ptr + dst_offset, screen_target.scanline,
        m_lens_buffer, m_lens_buffer_w, m_view_width, m_view_height, m_view_x, m_lens_type);

    m_capture_active = false;
    m_lens_buffer    = nullptr;
    m_lens_buffer_w  = 0;
    m_lens_buffer_h  = 0;
}

/******************************************************************************/
