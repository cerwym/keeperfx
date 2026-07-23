/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareUIRenderer.cpp
 *     Software implementation of the renderer-owned minimap pixel handoff.
 *     Everything else is inherited from IUIRenderer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareUIRenderer.h"
#include "renderer/ir/UICommands.h"
#include "renderer/RendererManager.h"
#include "bflib_video.h"   // pixel_size
#include "gui_draw.h"      // gui_slab, GUI_SLAB_DIMENSION
#include "post_inc.h"

#include <algorithm>

/******************************************************************************/

uint8_t* SoftwareUIRenderer::AcquireMinimapBuffer(int size)
{
    if (size <= 0)
        return nullptr;
    const size_t needed = (size_t)size * (size_t)size;
    if ((int)m_minimap_size != size)
    {
        m_minimap_buf.assign(needed, 0);
        m_minimap_size = size;
    }
    else
    {
        // clear it
        std::fill(m_minimap_buf.begin(), m_minimap_buf.end(), (uint8_t)0);
    }
    return m_minimap_buf.data();
}

void SoftwareUIRenderer::SubmitMinimap(int screen_x, int screen_y, int size)
{
    UICommandBuffers* cmds = m_ui_write_cmds;
    if (!cmds || size <= 0 || m_minimap_size != size)
        return;

    const size_t count = (size_t)size * (size_t)size;
    if (m_minimap_buf.size() < count)
        return;

    // Hand over the pixels we own.
    cmds->minimap_pixels.assign(m_minimap_buf.begin(), m_minimap_buf.begin() + count);
    cmds->minimap_blit.x    = screen_x;
    cmds->minimap_blit.y    = screen_y;
    cmds->minimap_blit.size = size;
    cmds->minimap_blit.seq  = cmds->NextSeq();
    cmds->minimap_blit.set  = true;
}

bool SoftwareUIRenderer::SubmitSlabBackground(int x, int y, int w, int h)
{
    TbPixel* screen = RendererGetWScreen();
    if (!screen || !gui_slab)
        return false;

    long i;
    long scr_x = x / pixel_size;
    long scr_y = y / pixel_size;
    long scr_h = h / pixel_size;
    long scr_w = w / pixel_size;
    if (scr_x < 0)
    {
        i = scr_x + w / pixel_size;
        scr_x = 0;
        scr_w = i;
    }
    if (scr_y < 0)
    {
        i = scr_y + scr_h;
        scr_y = 0;
        scr_h = i;
    }
    i = RendererPhysicalWidth() * pixel_size;
    if (scr_x + scr_w > i)
        scr_w = i - scr_x;
    i = RendererGetScreenHeight();
    if (scr_y + scr_h > i)
        scr_h = i - scr_y;
    if ((scr_w <= 0) || (scr_h <= 0))
        return true;

    TbPixel* out = &screen[scr_x + RendererScreenWidth() * scr_y];
    for (i = 0; scr_h > i; i++)
    {
        TbPixel* inp = &gui_slab[GUI_SLAB_DIMENSION * (i % GUI_SLAB_DIMENSION)];
        if (scr_w >= GUI_SLAB_DIMENSION)
        {
            for (int j = 0; j < GUI_SLAB_DIMENSION; j++) out[j] = inp[j] ? inp[j] : 1;
            int k;
            for (k = GUI_SLAB_DIMENSION; k < scr_w - GUI_SLAB_DIMENSION; k += GUI_SLAB_DIMENSION)
            {
                for (int j = 0; j < GUI_SLAB_DIMENSION; j++) (out + k)[j] = inp[j] ? inp[j] : 1;
            }
            if (w - k > 0) {
                for (int j = 0; j < scr_w - k; j++) (out + k)[j] = inp[j] ? inp[j] : 1;
            }
        } else
        {
            for (int j = 0; j < scr_w; j++) out[j] = inp[j] ? inp[j] : 1;
        }
        out += RendererScreenWidth();
    }
    return true;
}

/******************************************************************************/
