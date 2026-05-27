/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererBridge_Text.cpp
 *     C-callable TextRenderer wrappers (split from RendererManager.cpp).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "renderer/ITextRenderer.h"
#include "post_inc.h"

/******************************************************************************/

void TextRenderer_SetFont(const struct TbSpriteSheet* font)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->SetFont(font);
}

const struct TbSpriteSheet* TextRenderer_GetFont(void)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->GetFont();
    return NULL;
}

void TextRenderer_SetWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->SetWindow(x, y, w, h);
}

void TextRenderer_SetJustifyWindow(int32_t x, int32_t y, int32_t w)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->SetJustifyWindow(x, y, w);
}

void TextRenderer_SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->SetClipWindow(x, y, w, h);
}

void TextRenderer_GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->GetJustifyWindow(x, y, w);
}

void TextRenderer_GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->GetClipWindow(x, y, w, h);
}

TbBool TextRenderer_DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->DrawTextResized(posx, posy, units_per_px, text);
    return false;
}

TbBool TextRenderer_DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->DrawTextAt(screen_x, screen_y, units_per_px, text);
    return false;
}

void TextRenderer_Draw(void)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) tr->Draw();
}

int32_t TextRenderer_LineHeight(void)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->LineHeight();
    return 0;
}

int32_t TextRenderer_CharWidth(uint32_t chr)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->CharWidth(chr);
    return 0;
}

int32_t TextRenderer_CharWidthScaled(uint32_t chr, int32_t units_per_px)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->CharWidthScaled(chr, units_per_px);
    return 0;
}

int32_t TextRenderer_StringWidth(const char* text)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->StringWidth(text);
    return 0;
}

int32_t TextRenderer_StringWidthScaled(const char* text, int32_t units_per_px)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->StringWidthScaled(text, units_per_px);
    return 0;
}

int32_t TextRenderer_WordWidth(const char* str)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->WordWidth(str);
    return 0;
}

int32_t TextRenderer_WordWidthScaled(const char* str, int32_t units_per_px)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->WordWidthScaled(str, units_per_px);
    return 0;
}

int32_t TextRenderer_TextHeight(const char* text)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->TextHeight(text);
    return 0;
}

int32_t TextRenderer_StringHeight(int32_t units_per_px, const char* text)
{
    ITextRenderer* tr = RendererGetTextRenderer();
    if (tr) return tr->StringHeight(units_per_px, text);
    return 0;
}
