/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareTextRenderer.cpp
 *     CPU software implementation of ITextRenderer.
 *     Phase 1: Delegates to existing bflib_sprfnt.c functions.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareTextRenderer.h"

#include "bflib_sprfnt.h"
#include "post_inc.h"

/******************************************************************************/

void SoftwareTextRenderer::SetFont(const struct TbSpriteSheet* font)
{
    LbTextSetFont(font);
}

void SoftwareTextRenderer::SetWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LbTextSetWindow(x, y, w, h);
}

void SoftwareTextRenderer::SetJustifyWindow(int32_t x, int32_t y, int32_t w)
{
    LbTextSetJustifyWindow(x, y, w);
}

void SoftwareTextRenderer::SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    LbTextSetClipWindow(x, y, w, h);
}

void SoftwareTextRenderer::GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const
{
    // bflib uses int, but int32_t is the same on all supported platforms
    LbTextGetJustifyWindow(reinterpret_cast<int*>(x),
                           reinterpret_cast<int*>(y),
                           reinterpret_cast<int*>(w));
}

void SoftwareTextRenderer::GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const
{
    LbTextGetClipWindow(reinterpret_cast<int*>(x),
                        reinterpret_cast<int*>(y),
                        reinterpret_cast<int*>(w),
                        reinterpret_cast<int*>(h));
}

TbBool SoftwareTextRenderer::DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text)
{
    return LbTextDrawResized_sw(posx, posy, units_per_px, text);
}

TbBool SoftwareTextRenderer::DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text)
{
    // Phase 1 stub: set a trivial full-screen window, draw, restore nothing (caller sets window)
    // Phase 2+ will implement single-line direct draw without touching window state
    return LbTextDrawResized_sw(screen_x, screen_y, units_per_px, text);
}

int32_t SoftwareTextRenderer::LineHeight()
{
    return LbTextLineHeight();
}

int32_t SoftwareTextRenderer::CharWidth(uint32_t chr)
{
    return LbTextCharWidth(static_cast<long>(chr));
}

int32_t SoftwareTextRenderer::CharWidthScaled(uint32_t chr, int32_t units_per_px)
{
    return LbTextCharWidthM(static_cast<long>(chr), units_per_px);
}

int32_t SoftwareTextRenderer::StringWidth(const char* text)
{
    return LbTextStringWidth(text);
}

int32_t SoftwareTextRenderer::StringWidthScaled(const char* text, int32_t units_per_px)
{
    return LbTextStringWidthM(text, units_per_px);
}

int32_t SoftwareTextRenderer::WordWidth(const char* str)
{
    return LbTextWordWidth(str);
}

int32_t SoftwareTextRenderer::WordWidthScaled(const char* str, int32_t units_per_px)
{
    return LbTextWordWidthM(str, units_per_px);
}

int32_t SoftwareTextRenderer::TextHeight(const char* text)
{
    return LbTextHeight(text);
}

int32_t SoftwareTextRenderer::StringHeight(int32_t units_per_px, const char* text)
{
    return static_cast<int32_t>(text_string_height(units_per_px, text));
}
