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

void SoftwareLensRenderer::BeginWorldCapture()
{
    m_capture_active = false;
    if (!lens_is_ready())
        return;

    m_lens_buffer   = lens_get_render_target();
    m_lens_buffer_w = lens_get_render_target_width();
    m_lens_buffer_h = lens_get_render_target_height();
    if (!m_lens_buffer || m_lens_buffer_w == 0 || m_lens_buffer_h == 0)
        return;

    m_saved_wscreen    = RendererGetWScreen();
    m_saved_graphics_w = RendererScreenWidth();
    m_saved_graphics_h = RendererScreenHeight();
    RendererStoreViewport(&m_saved_viewport);

    memset(m_lens_buffer, 0, (size_t)m_lens_buffer_w * (size_t)m_lens_buffer_h * sizeof(TbPixel));
    RendererSetWScreen(m_lens_buffer);
    RendererSetScreenDimensions((int)m_lens_buffer_w, (int)m_lens_buffer_h);
    RendererSetViewport(0, 0, RendererScreenWidth(), RendererScreenHeight());
    setup_engine_window(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());
    m_capture_active = true;
}

void SoftwareLensRenderer::EndWorldCapture()
{
    if (!m_capture_active)
        return;

    struct PlayerInfo* player = get_my_player();
    const long view_width  = player->engine_window_width / pixel_size;
    const long view_height = player->engine_window_height / pixel_size;
    const long view_x      = player->engine_window_x / pixel_size;
    const long view_y      = player->engine_window_y / pixel_size;

    RendererSetWScreen(m_saved_wscreen);
    RendererSetScreenDimensions(m_saved_graphics_w, m_saved_graphics_h);
    RendererLoadViewport(&m_saved_viewport);
    setup_engine_window(0, 0, RendererGetScreenWidth(), RendererGetScreenHeight());

    const long dst_offset = view_y * RendererScreenWidth() + view_x;
    draw_lens_effect(RendererGetWScreen() + dst_offset, RendererScreenWidth(),
        m_lens_buffer, m_lens_buffer_w, view_width, view_height, view_x, game.applied_lens_type);

    m_capture_active = false;
    m_lens_buffer    = nullptr;
    m_lens_buffer_w  = 0;
    m_lens_buffer_h  = 0;
}

/******************************************************************************/
