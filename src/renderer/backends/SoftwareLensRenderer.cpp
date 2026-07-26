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

void SoftwareLensRenderer::ResolveWorldCaptureBegin()
{
    if (!m_capture_active)
        return;

    m_saved_wscreen    = RendererGetWScreen();
    m_saved_graphics_w = RendererScreenWidth();
    m_saved_graphics_h = RendererScreenHeight();
    RendererStoreViewport(&m_saved_viewport);

    memset(m_lens_buffer, 0, (size_t)m_lens_buffer_w * (size_t)m_lens_buffer_h * sizeof(TbPixel));
    RendererSetWScreen(m_lens_buffer);
    RendererSetScreenDimensions((int)m_lens_buffer_w, (int)m_lens_buffer_h);
    RendererSetViewport(0, 0, (int)m_lens_buffer_w, (int)m_lens_buffer_h);
}

void SoftwareLensRenderer::ResolveWorldCaptureEnd()
{
    if (!m_capture_active)
        return;

    RendererSetWScreen(m_saved_wscreen);
    RendererSetScreenDimensions(m_saved_graphics_w, m_saved_graphics_h);
    RendererLoadViewport(&m_saved_viewport);

    const long dst_offset = m_view_y * RendererScreenWidth() + m_view_x;
    draw_lens_effect(RendererGetWScreen() + dst_offset, RendererScreenWidth(),
        m_lens_buffer, m_lens_buffer_w, m_view_width, m_view_height, m_view_x, m_lens_type);

    m_capture_active = false;
    m_lens_buffer    = nullptr;
    m_lens_buffer_w  = 0;
    m_lens_buffer_h  = 0;
}

/******************************************************************************/
