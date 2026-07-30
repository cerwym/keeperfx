/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareMapFadePass.cpp
 *     CPU software implementation of IMapFadePass.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareMapFadePass.h"
#include "engine_redraw.h"
#include "engine_render.h"
#include "front_simple.h"
#include "gui_parchment.h"
#include "player_data.h"
#include "renderer/RendererManager.h"
#include "vidmode.h"
#include "vidfade.h"

extern "C" {
void map_fade(unsigned char *outbuf, unsigned char *srcbuf1, unsigned char *srcbuf2,
    unsigned char *fade_tbl, unsigned char *ghost_tbl, long a6, long const xmax,
    long const ymax, long a9);
void generate_map_fade_ghost_table(const char *fname, unsigned char *palette, unsigned char *ghost_table);
}
#include "post_inc.h"
 
/******************************************************************************/

void SoftwareMapFadePass::PrepareBuffers(uint8_t* fade_src, uint8_t* fade_dest, int scanline, int height)
{
    struct PlayerInfo* player = get_my_player();
    if (player->view_mode_restore == PVM_IsoWibbleView || player->view_mode_restore == PVM_IsoStraightView)
        redraw_isometric_view();
    else
        redraw_frontview();

    RendererExecutePendingWorld();

    TbGraphicsWindow target = RendererGetDrawTarget();
    int fadebuf_pos = 0;
    for (int i = 0; i < height; i++)
    {
        unsigned char* src = target.ptr + target.scanline * i;
        unsigned char* dst = &fade_src[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, target.scanline);
    }

    load_parchment_file();
    redraw_minimal_overhead_view();

    // The target base may have moved during the redraw above; re-fetch it.
    target = RendererGetDrawTarget();
    fadebuf_pos = 0;
    for (int i = 0; i < height; i++)
    {
        unsigned char* src = target.ptr + target.scanline * i;
        unsigned char* dst = &fade_dest[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, target.scanline);
    }
}
 
void SoftwareMapFadePass::EnsureBuffers()
{
    const TbGraphicsWindow target = RendererGetDrawTarget();
    const size_t ghost_bytes = (size_t)PALETTE_COLORS * PALETTE_COLORS;
    const size_t capture_extent = 320 * (size_t)target.screen_height + target.scanline;
    m_fade_buffer.resize(ghost_bytes + 320 * 200 + capture_extent);
    m_map_fade_ghost_table = m_fade_buffer.data();
    m_map_fade_src = m_map_fade_ghost_table + ghost_bytes;
    m_map_fade_dest = m_map_fade_src + 320 * 200;
}

int32_t SoftwareMapFadePass::StepFadeIn(int32_t step)
{
    if (step == 0)
    {
        EnsureBuffers();
        PrepareBuffers(m_map_fade_src, m_map_fade_dest, 320, RendererGetDrawTarget().screen_height);
        generate_map_fade_ghost_table("data/mapfadeg.dat", engine_palette, m_map_fade_ghost_table);
    }
    const TbGraphicsWindow target = RendererGetDrawTarget();
    map_fade(target.ptr, m_map_fade_dest, m_map_fade_src, pixmap.fade_tables,
        m_map_fade_ghost_table, step, 320, 200, target.scanline);
    return (8 - get_my_player()->instance_remain_turns) * 4;
}
 
int32_t SoftwareMapFadePass::StepFadeOut(int32_t step)
{
    if (step == 32)
    {
        EnsureBuffers();
        PrepareBuffers(m_map_fade_src, m_map_fade_dest, 320, RendererGetDrawTarget().screen_height);
        generate_map_fade_ghost_table("data/mapfadeg.dat", engine_palette, m_map_fade_ghost_table);
    }
    const TbGraphicsWindow target = RendererGetDrawTarget();
    map_fade(target.ptr, m_map_fade_dest, m_map_fade_src, pixmap.fade_tables,
        m_map_fade_ghost_table, step, 320, 200, target.scanline);
    return get_my_player()->instance_remain_turns * 4;
}
