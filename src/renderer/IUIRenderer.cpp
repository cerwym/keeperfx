/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.cpp
 *     Default (CPU) implementations for IUIRenderer.
 * @par Design:
 *     Every virtual method defined here is the authoritative CPU fallback.
 *     GPU subclasses override only the methods they accelerate; they never need
 *     to re-implement the CPU path because the base already provides it.
 *
 *     This file was seeded from SoftwareUIRenderer.cpp (which is now a trivial
 *     empty subclass that just supplies a different GetName()).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/IUIRenderer.h"
#include "engine_render.h"       // process_keeper_sprite
#include "renderer/RendererManager.h"
#include "bflib_vidraw.h"        // sprite and primitive CPU fallbacks
#include "bflib_sprite.h"        // TbSprite
#include "bflib_video.h"         // lbDisplay, units_per_pixel
#include "vidmode.h"             // pixmap (TbColorTables) for fade_tables

extern "C" {
extern unsigned char* MapBackground;
extern int32_t* MapShapeStart;
extern int32_t* MapShapeEnd;
extern long NumBackColours;
extern unsigned char MapBackColours[256];
}

namespace {
class ScopedSpriteSubmitGuard {
public:
    ScopedSpriteSubmitGuard()
        : m_saved(lb_in_sprite_submit)
    {
        lb_in_sprite_submit = 1;
    }

    ~ScopedSpriteSubmitGuard()
    {
        lb_in_sprite_submit = m_saved;
    }

private:
    int m_saved;
};
}

// ---------------------------------------------------------------------------
// Sprite handle registry
// ---------------------------------------------------------------------------

void IUIRenderer::RegisterSpriteHandle(SpriteHandle h, const struct TbSprite* spr)
{
    m_handle_to_sprite[h] = spr;
}

// ---------------------------------------------------------------------------
// Default (CPU) submission implementations
// ---------------------------------------------------------------------------

void IUIRenderer::SubmitSlabSelector(int x1, int y1, int x2, int y2,
                                     unsigned char color, float z_depth)
{
    // Software renderer draws selectors through the staging buffer directly;
    // the GPU path overrides this with a line-draw quad.
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)z_depth;
}

void IUIRenderer::BeginZoomBoxOverlay(int x, int y, int w, int h)
{
    const int pad = (2 * units_per_pixel) / 16;
    RendererSetViewport(x + pad, y + pad, w - 2 * pad, h - 2 * pad);
}

void IUIRenderer::EndZoomBoxOverlay(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
    RendererSetViewport(0, 0, RendererScreenWidth(), RendererScreenHeight());
}

void IUIRenderer::SetupMinimapBackground(int diaglen, int panel_x, int panel_y)
{
    if (!MapBackground || !MapShapeStart || !MapShapeEnd || !lbDisplay.WScreen)
    {
        NumBackColours = 0;
        return;
    }

    int num_colours = 0;
    long bkgnd_pos = 0;
    TbPixel* out = &lbDisplay.WScreen[panel_x + lbDisplay.GraphicsScreenWidth * panel_y];
    for (int h = 0; h < diaglen; h++)
    {
        for (int w = MapShapeStart[h]; w < MapShapeEnd[h]; w++)
        {
            if (w < 0) continue;

            const TbPixel orig = out[w];
            out[w] = 255;
            int colour;
            for (colour = 0; colour < num_colours; colour++)
            {
                if (MapBackColours[colour] == orig)
                    break;
            }
            if (num_colours == colour)
            {
                MapBackColours[num_colours] = orig;
                num_colours++;
            }
            MapBackground[bkgnd_pos + w] = (unsigned char)colour;
        }
        bkgnd_pos += diaglen;
        out += lbDisplay.GraphicsScreenWidth;
    }
    NumBackColours = num_colours;
}

bool IUIRenderer::GetMinimapOpaqueBlackIndex(uint8_t* idx) const
{
    (void)idx;
    return false;
}

void IUIRenderer::SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                    SpriteHandle spr, bool flip_horiz,
                                    unsigned int draw_flags)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    unsigned int effective_flags = draw_flags;
    if (flip_horiz) effective_flags |= Lb_SPRITE_FLIP_HORIZ;
    unsigned int saved = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = effective_flags;
    ScopedSpriteSubmitGuard guard;
    LbSpriteDrawResized(x, y, units_per_px, it->second);
    lbDisplay.DrawFlags = saved;
}

void IUIRenderer::SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                         SpriteHandle spr, int remap_row,
                                         unsigned int draw_flags)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    unsigned int saved = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    LbSpriteDrawResizedRemap(x, y, units_per_px, it->second,
                             &pixmap.fade_tables[remap_row * 256]);
    lbDisplay.DrawFlags = saved;
}

void IUIRenderer::SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                           SpriteHandle spr, uint8_t color_idx,
                                           unsigned int draw_flags)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    unsigned int saved = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    LbSpriteDrawResizedOneColour(x, y, units_per_px, it->second, color_idx);
    lbDisplay.DrawFlags = saved;
}

void IUIRenderer::SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                     SpriteHandle spr, unsigned int draw_flags)
{
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    unsigned int saved = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    LbSpriteDrawScaled(x, y, it->second, w, h);
    lbDisplay.DrawFlags = saved;
}

void IUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx)
{
    if (w <= 0 || h <= 0) return;
    if (lbDisplay.DrawFlags & Lb_SPRITE_OUTLINE)
    {
        if (w < 1 || h < 1) return;
        unsigned short saved_flags = lbDisplay.DrawFlags;
        lbDisplay.DrawFlags &= ~Lb_SPRITE_OUTLINE;
        LbDrawBoxClip(x, y, (unsigned long)w, 1, color_idx);
        LbDrawBoxClip(x, y + h - 1, (unsigned long)w, 1, color_idx);
        if (h > 2)
        {
            LbDrawBoxClip(x, y + 1, 1, (unsigned long)(h - 2), color_idx);
            LbDrawBoxClip(x + w - 1, y + 1, 1, (unsigned long)(h - 2), color_idx);
        }
        lbDisplay.DrawFlags = saved_flags;
        return;
    }
    LbDrawBoxClip(x, y, (unsigned long)w, (unsigned long)h, color_idx);
}
 
void IUIRenderer::SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha)
{
    if (w <= 0 || h <= 0) return;
    unsigned short saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags &= ~(Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8);
    if (alpha < 0.75f)
        lbDisplay.DrawFlags |= Lb_SPRITE_TRANSPAR4;
    else if (alpha < 0.9f)
        lbDisplay.DrawFlags |= Lb_SPRITE_TRANSPAR8;
    LbDrawBoxClip(x, y, (unsigned long)w, (unsigned long)h, color_idx);
    lbDisplay.DrawFlags = saved_flags;
}

void IUIRenderer::SubmitCircle(int32_t x, int32_t y, int32_t radius, uint8_t color_idx)
{
    LbDrawCircle(x, y, radius, color_idx);
}

TbResult IUIRenderer::SubmitRawSprite(long x, long y, const struct TbSprite* spr,
                                      unsigned int draw_flags)
{
    if (!spr) return Lb_FAIL;
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDraw(x, y, spr);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult IUIRenderer::SubmitRawSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                               unsigned char colour, unsigned int draw_flags)
{
    if (!spr) return Lb_FAIL;
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDrawOneColour(x, y, spr, colour);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

TbResult IUIRenderer::SubmitRawSpriteRemap(long x, long y, const struct TbSprite* spr,
                                           const unsigned char* cmap, unsigned int draw_flags)
{
    if (!spr || !cmap) return Lb_FAIL;
    unsigned int saved_flags = lbDisplay.DrawFlags;
    lbDisplay.DrawFlags = draw_flags;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDrawScaledRemap(x, y, spr, spr->SWidth, spr->SHeight, cmap);
    lbDisplay.DrawFlags = saved_flags;
    return ret;
}

uint8_t* IUIRenderer::AcquireMinimapBuffer(int /*size*/)
{
    // CPU mode: caller writes directly to lbDisplay.WScreen.
    return nullptr;
}

void IUIRenderer::SubmitMinimap(int /*screen_x*/, int /*screen_y*/, int /*size*/)
{
    // No-op: minimap pixels were written directly to lbDisplay.WScreen.
}

void IUIRenderer::Draw()
{
    // No-op: CPU path flushes through existing staging-buffer blit.
}

void IUIRenderer::Clear()
{
    // No-op: CPU path clears through existing paths.
}

const char* IUIRenderer::GetName() const
{
    return "CPU_UI";
}

#include "post_inc.h"
