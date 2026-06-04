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

    int fadebuf_pos = 0;
    for (int i = 0; i < height; i++)
    {
        unsigned char* src = RendererGetWScreen() + lbDisplay.GraphicsScreenWidth * i;
        unsigned char* dst = &fade_src[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, RendererScreenWidth());
    }

    load_parchment_file();
    redraw_minimal_overhead_view();

    fadebuf_pos = 0;
    for (int i = 0; i < height; i++)
    {
        unsigned char* src = RendererGetWScreen() + lbDisplay.GraphicsScreenWidth * i;
        unsigned char* dst = &fade_dest[fadebuf_pos];
        fadebuf_pos += scanline;
        memcpy(dst, src, RendererScreenWidth());
    }
}
 
int32_t SoftwareMapFadePass::StepFadeIn(int32_t step)
{
    if (step == 0)
    {
        m_map_fade_ghost_table = poly_pool;
        m_map_fade_src = poly_pool + PALETTE_COLORS * PALETTE_COLORS;
        m_map_fade_dest = m_map_fade_src + 320 * 200;
        PrepareBuffers(m_map_fade_src, m_map_fade_dest, 320, RendererScreenHeight());
        generate_map_fade_ghost_table("data/mapfadeg.dat", engine_palette, m_map_fade_ghost_table);
    }
    map_fade(RendererGetWScreen(), m_map_fade_dest, m_map_fade_src, pixmap.fade_tables,
        m_map_fade_ghost_table, step, 320, 200, lbDisplay.GraphicsScreenWidth);
    return (8 - get_my_player()->instance_remain_turns) * 4;
}
 
int32_t SoftwareMapFadePass::StepFadeOut(int32_t step)
{
    if (step == 32)
    {
        m_map_fade_ghost_table = poly_pool;
        m_map_fade_src = poly_pool + PALETTE_COLORS * PALETTE_COLORS;
        m_map_fade_dest = m_map_fade_src + 320 * 200;
        PrepareBuffers(m_map_fade_src, m_map_fade_dest, 320, RendererScreenHeight());
        generate_map_fade_ghost_table("data/mapfadeg.dat", engine_palette, m_map_fade_ghost_table);
    }
    map_fade(RendererGetWScreen(), m_map_fade_dest, m_map_fade_src, pixmap.fade_tables,
        m_map_fade_ghost_table, step, 320, 200, lbDisplay.GraphicsScreenWidth);
    return get_my_player()->instance_remain_turns * 4;
}
