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
#include "renderer/ITextRenderer.h"      // ReplayTextCommand for the merged replay
#include "renderer/ir/UICommands.h"      // IR command types for append/replay
#include "renderer/ir/TextCommands.h"    // IRTextDrawCmd for the merged replay
#include "bflib_vidraw.h"        // sprite and primitive CPU fallbacks
#include "bflib_sprite.h"        // TbSprite
#include "bflib_video.h"         // lbDisplay, units_per_pixel
#include "vidmode.h"
#include "vidfade.h"             // pixmap (TbColorTables) for fade_tables
#include <algorithm>             // std::sort for the seq-merge replay
#include <vector>

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
    m_sprite_to_handle[spr] = h;
}

SpriteHandle IUIRenderer::ResolveSprite(const struct TbSprite* spr)
{
    // Zero-dimension / dataless sprites are sentinel entries that never draw.
    if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0)
        return kInvalidSpriteHandle;
    auto it = m_sprite_to_handle.find(spr);
    if (it != m_sprite_to_handle.end())
        return it->second;
    SpriteHandle h = m_next_handle++;
    RegisterSpriteHandle(h, spr);
    return h;
}

void IUIRenderer::RegisterSpriteSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    const long n = num_sprites(sheet);
    for (long i = 0; i < n; ++i)
        ResolveSprite(get_sprite(sheet, i));
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
    if (!MapBackground || !MapShapeStart || !MapShapeEnd || !RendererGetWScreen())
    {
        NumBackColours = 0;
        return;
    }

    int num_colours = 0;
    long bkgnd_pos = 0;
    TbPixel* out = &RendererGetWScreen()[panel_x + RendererScreenWidth() * panel_y];
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
        out += RendererScreenWidth();
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
                                    KfxDrawState state)
{
    if (m_ui_write_cmds) {
        IRUISpriteCmd cmd;
        cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px;
        cmd.sprite = spr;
        cmd.flags = flip_horiz ? kIRSpriteFlipHoriz : 0u;
        cmd.draw_flags = state.flags;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites.Append(cmd);
        return;
    }
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    unsigned int effective_flags = state.flags;
    if (flip_horiz) effective_flags |= Lb_SPRITE_FLIP_HORIZ;
    ScopedSpriteSubmitGuard guard;
    LbSpriteDrawResized(x, y, units_per_px, it->second, effective_flags);
}

void IUIRenderer::SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                         SpriteHandle spr, int remap_row,
                                         KfxDrawState state)
{
    if (m_ui_write_cmds) {
        IRUISpriteRemapCmd cmd;
        cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px;
        cmd.sprite = spr; cmd.remap_row = remap_row;
        cmd.draw_flags = state.flags;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_remap.Append(cmd);
        return;
    }
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    LbSpriteDrawResizedRemap(x, y, units_per_px, it->second,
                             &pixmap.fade_tables[remap_row * 256], state.flags);
}

void IUIRenderer::SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                           SpriteHandle spr, uint8_t color_idx,
                                           KfxDrawState state)
{
    if (m_ui_write_cmds) {
        IRUISpriteColoredCmd cmd;
        cmd.x = x; cmd.y = y; cmd.units_per_px = units_per_px;
        cmd.sprite = spr; cmd.colour_idx = color_idx;
        cmd.draw_flags = state.flags;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites_colored.Append(cmd);
        return;
    }
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    LbSpriteDrawResizedOneColour(x, y, units_per_px, it->second, color_idx, state.flags);
}

void IUIRenderer::SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                     SpriteHandle spr, KfxDrawState state)
{
    if (m_ui_write_cmds) {
        IRUISpriteCmd cmd;
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h; cmd.units_per_px = 16;
        cmd.sprite = spr; cmd.flags = kIRSpriteScaled;
        cmd.draw_flags = state.flags;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->sprites.Append(cmd);
        return;
    }
    auto it = m_handle_to_sprite.find(spr);
    if (it == m_handle_to_sprite.end()) return;
    ScopedSpriteSubmitGuard guard;
    LbSpriteDrawScaled(x, y, it->second, w, h, state.flags);
}

void IUIRenderer::SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, KfxDrawState state)
{
    if (w <= 0 || h <= 0) return;
    if (m_ui_write_cmds) {
        IRUISolidBoxCmd cmd;
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.colour_idx = color_idx; cmd.alpha = 1.0f;
        cmd.draw_flags = state.flags;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->solid_boxes.Append(cmd);
        return;
    }
    if (state.flags & Lb_SPRITE_OUTLINE)
    {
        if (w < 1 || h < 1) return;
        TbDrawFlagsMask box_flags = state.flags & ~Lb_SPRITE_OUTLINE;
        LbDrawBoxClip(x, y, (unsigned long)w, 1, color_idx, box_flags);
        LbDrawBoxClip(x, y + h - 1, (unsigned long)w, 1, color_idx, box_flags);
        if (h > 2)
        {
            LbDrawBoxClip(x, y + 1, 1, (unsigned long)(h - 2), color_idx, box_flags);
            LbDrawBoxClip(x + w - 1, y + 1, 1, (unsigned long)(h - 2), color_idx, box_flags);
        }
        return;
    }
    LbDrawBoxClip(x, y, (unsigned long)w, (unsigned long)h, color_idx, state.flags);
}
 
void IUIRenderer::SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha)
{
    if (w <= 0 || h <= 0) return;
    if (m_ui_write_cmds) {
        IRUISolidBoxCmd cmd;
        cmd.x = x; cmd.y = y; cmd.w = w; cmd.h = h;
        cmd.colour_idx = color_idx; cmd.alpha = alpha; cmd.draw_flags = 0;
        cmd.seq = m_ui_write_cmds->NextSeq();
        m_ui_write_cmds->solid_boxes.Append(cmd);
        return;
    }
    TbDrawFlagsMask box_flags = 0;
    if (alpha < 0.75f)
        box_flags |= Lb_SPRITE_TRANSPAR4;
    else if (alpha < 0.9f)
        box_flags |= Lb_SPRITE_TRANSPAR8;
    LbDrawBoxClip(x, y, (unsigned long)w, (unsigned long)h, color_idx, box_flags);
}

TbResult IUIRenderer::SubmitRawSprite(long x, long y, const struct TbSprite* spr,
                                      KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDraw(x, y, spr, state.flags);
    return ret;
}

TbResult IUIRenderer::SubmitRawSpriteOneColour(long x, long y, const struct TbSprite* spr,
                                               unsigned char colour, KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDrawOneColour(x, y, spr, colour, state.flags);
    return ret;
}

TbResult IUIRenderer::SubmitRawSpriteRemap(long x, long y, const struct TbSprite* spr,
                                           const unsigned char* cmap, KfxDrawState state)
{
    if (!spr || !cmap) return Lb_FAIL;
    ScopedSpriteSubmitGuard guard;
    TbResult ret = LbSpriteDrawScaledRemap(x, y, spr, spr->SWidth, spr->SHeight, cmap, state.flags);
    return ret;
}

uint8_t* IUIRenderer::AcquireMinimapBuffer(int /*size*/)
{
    // CPU mode: caller writes directly to RendererGetWScreen().
    return nullptr;
}

void IUIRenderer::SubmitMinimap(int /*screen_x*/, int /*screen_y*/, int /*size*/)
{
    // No-op: minimap pixels were written directly to RendererGetWScreen().
}

void IUIRenderer::SetUICommandBuffers(UICommandBuffers* cmds)
{
    m_ui_write_cmds = cmds;
    if (cmds) cmds->ir_active = true;
}

void IUIRenderer::ReplayMergedFromIR(const UICommandBuffers& ui,
                                     const TextCommandBuffers& text,
                                     ITextRenderer* text_renderer)
{
    // Merge UI sprite commands and text draws by their shared submission seq,
    // then replay each through the immediate CPU path in true submission order.
    // (Boxes/circles/raw sprites are not deferred yet — they stay immediate as
    // background draws under the deferred foreground; see docs.)
    struct Ref { uint32_t seq; uint8_t kind; uint32_t idx; };
    enum { K_Sprite = 0, K_Remap, K_Colored, K_Box, K_Text };
    std::vector<Ref> order;
    order.reserve(ui.sprites.Size() + ui.sprites_remap.Size() +
                  ui.sprites_colored.Size() + ui.solid_boxes.Size() + text.draws.Size());
    for (uint32_t i = 0; i < (uint32_t)ui.sprites.Size(); ++i)
        order.push_back({ ui.sprites.Data()[i].seq, K_Sprite, i });
    for (uint32_t i = 0; i < (uint32_t)ui.sprites_remap.Size(); ++i)
        order.push_back({ ui.sprites_remap.Data()[i].seq, K_Remap, i });
    for (uint32_t i = 0; i < (uint32_t)ui.sprites_colored.Size(); ++i)
        order.push_back({ ui.sprites_colored.Data()[i].seq, K_Colored, i });
    for (uint32_t i = 0; i < (uint32_t)ui.solid_boxes.Size(); ++i)
        order.push_back({ ui.solid_boxes.Data()[i].seq, K_Box, i });
    if (text_renderer)
        for (uint32_t i = 0; i < (uint32_t)text.draws.Size(); ++i)
            order.push_back({ text.draws.Data()[i].seq, K_Text, i });

    std::sort(order.begin(), order.end(),
              [](const Ref& a, const Ref& b) { return a.seq < b.seq; });

    // Detach the write buffer so the immediate Submit* bodies draw instead of
    // re-appending.
    UICommandBuffers* saved = m_ui_write_cmds;
    m_ui_write_cmds = nullptr;
    for (const Ref& r : order)
    {
        switch (r.kind)
        {
        case K_Sprite: {
            const IRUISpriteCmd& c = ui.sprites.Data()[r.idx];
            if (c.flags & kIRSpriteScaled)
                SubmitScaledSprite(c.x, c.y, c.w, c.h, c.sprite, draw_state_make(c.draw_flags, 0));
            else
                SubmitPanelSprite(c.x, c.y, c.units_per_px, c.sprite,
                                  (c.flags & kIRSpriteFlipHoriz) != 0, draw_state_make(c.draw_flags, 0));
            break;
        }
        case K_Remap: {
            const IRUISpriteRemapCmd& c = ui.sprites_remap.Data()[r.idx];
            SubmitPanelSpriteRemap(c.x, c.y, c.units_per_px, c.sprite,
                                   c.remap_row, draw_state_make(c.draw_flags, 0));
            break;
        }
        case K_Colored: {
            const IRUISpriteColoredCmd& c = ui.sprites_colored.Data()[r.idx];
            SubmitPanelSpriteColored(c.x, c.y, c.units_per_px, c.sprite,
                                     c.colour_idx, draw_state_make(c.draw_flags, 0));
            break;
        }
        case K_Box: {
            const IRUISolidBoxCmd& c = ui.solid_boxes.Data()[r.idx];
            if (c.alpha < 1.0f) {
                SubmitSolidBoxAlpha(c.x, c.y, c.w, c.h, c.colour_idx, c.alpha);
            } else {
                SubmitSolidBox(c.x, c.y, c.w, c.h, c.colour_idx,
                               draw_state_make(c.draw_flags, 0));
            }
            break;
        }
        case K_Text:
            text_renderer->ReplayTextCommand(text.draws.Data()[r.idx]);
            break;
        }
    }
    m_ui_write_cmds = saved;
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
