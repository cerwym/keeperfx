/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareTextRenderer.cpp
 *     CPU software implementation of ITextRenderer.
 *     Owns font, DBC, and window state.  Drawing functions moved from
 *     bflib_sprfnt.c — sprite blits still go through lbDisplay globals.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/backends/SoftwareTextRenderer.h"
#include "renderer/RendererManager.h"
#include "renderer/ir/TextCommands.h"    // IRTextDrawCmd for software deferral

#include "bflib_sprfnt.h"
#include "bflib_sprite.h"
#include "bflib_vidraw.h"
#include "frontend.h"       // frontend_font[], winfont, font_sprites, frontstory_font
#include "front_credits.h"  // frontstory_font (may be declared here)
#include "post_inc.h"

/******************************************************************************/

SoftwareTextRenderer::SoftwareTextRenderer()
    : m_font(nullptr)
    , m_dbc_font(nullptr)
    , m_dbc_colour0(0)
    , m_dbc_colour1(0)
    , m_dbc_enabled(false)
    , m_justify_window{}
    , m_clip_window{}
{
}

/******************************************************************************/
/* Font                                                                       */
/******************************************************************************/

void SoftwareTextRenderer::SetFont(const struct TbSpriteSheet* font)
{
    m_font = font;

    if (dbc_initialized)
    {
        m_dbc_colour0 = LbTextGetFontFaceColor(font);
        m_dbc_colour1 = LbTextGetFontBackColor(font);

        // Resolve DBC font index from the Western font identity
        int dbc_idx;
        if (font == frontend_font[0]) {
            dbc_idx = 2;
        } else if (font == frontend_font[1] || font == frontend_font[2] ||
                   font == frontend_font[3] || font == winfont ||
                   font == font_sprites || font == frontstory_font) {
            dbc_idx = (RendererPhysicalWidth() < 512) ? 0 : 1;
        } else {
            dbc_idx = (RendererPhysicalWidth() < 512) ? 0 : 1;
        }

        const int32_t fonts_count = dbc_fonts_count();
        struct AsianFont* dbcfonts = dbc_fonts_list();
        if ((dbc_idx >= 0) && (dbc_idx < fonts_count) && (dbcfonts != nullptr))
        {
            m_dbc_font = &dbcfonts[dbc_idx];
            m_dbc_enabled = true;
        }
        else
        {
            m_dbc_font = nullptr;
            m_dbc_enabled = false;
        }

        // Keep globals in sync during transition
        active_dbcfont = const_cast<struct AsianFont*>(m_dbc_font);
    }
    else
    {
        m_dbc_font = nullptr;
        m_dbc_enabled = false;
    }
}

/******************************************************************************/
/* Windowing                                                                  */
/******************************************************************************/

void SoftwareTextRenderer::SetWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    m_justify_window = { x, y, w, 0 };
    SetClipWindow(x, y, w, h);
}

void SoftwareTextRenderer::SetJustifyWindow(int32_t x, int32_t y, int32_t w)
{
    m_justify_window.x = x;
    m_justify_window.y = y;
    m_justify_window.width = w;
}

void SoftwareTextRenderer::SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x0 = x, y0 = y;
    int32_t x1 = x + w, y1 = y + h;
    if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 < 0) x1 = 0;
    if (y0 < 0) y0 = 0;
    if (y1 < 0) y1 = 0;
    if (x0 > RendererScreenWidth())  x0 = RendererScreenWidth();
    if (x1 > RendererScreenWidth())  x1 = RendererScreenWidth();
    if (y0 > RendererScreenHeight()) y0 = RendererScreenHeight();
    if (y1 > RendererScreenHeight()) y1 = RendererScreenHeight();

    m_clip_window = { x0, y0, x1 - x0, y1 - y0 };
}

void SoftwareTextRenderer::GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const
{
    if (x) *x = m_justify_window.x;
    if (y) *y = m_justify_window.y;
    if (w) *w = m_justify_window.width;
}

void SoftwareTextRenderer::GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const
{
    if (x) *x = m_clip_window.x;
    if (y) *y = m_clip_window.y;
    if (w) *w = m_clip_window.width;
    if (h) *h = m_clip_window.height;
}

/******************************************************************************/
/* Measurement                                                                */
/******************************************************************************/

int32_t SoftwareTextRenderer::LineHeight()
{
    if (m_dbc_enabled)
        return static_cast<int32_t>(dbc_char_height(0xFFFF));
    return LbSprFontCharHeight(m_font, ' ');
}

int32_t SoftwareTextRenderer::CharWidth(uint32_t chr)
{
    if (m_dbc_enabled)
        return static_cast<int32_t>(dbc_char_width(chr));
    return LbSprFontCharWidth(m_font, static_cast<unsigned char>(chr));
}

int32_t SoftwareTextRenderer::CharWidthScaled(uint32_t chr, int32_t units_per_px)
{
    if (m_dbc_enabled)
        return static_cast<int32_t>(dbc_char_widthM(chr, units_per_px));
    return LbSprFontCharWidth(m_font, static_cast<unsigned char>(chr)) * units_per_px / 16;
}

int32_t SoftwareTextRenderer::StringWidth(const char* text)
{
    return LbTextStringPartWidth(text, INT_MAX);
}

int32_t SoftwareTextRenderer::StringWidthScaled(const char* text, int32_t units_per_px)
{
    if (m_dbc_enabled)
        return LbTextStringPartWidthM(text, INT_MAX, units_per_px);
    return StringWidth(text) * units_per_px / 16;
}

int32_t SoftwareTextRenderer::WordWidth(const char* str)
{
    return LbSprFontWordWidth(m_font, str);
}

int32_t SoftwareTextRenderer::WordWidthScaled(const char* str, int32_t units_per_px)
{
    if (!str || str[0] == 0)
        return 0;

    if (m_dbc_enabled)
    {
        int32_t len = 0;
        for (int i = 0; str[i] != 0; i++)
        {
            unsigned char c = str[i];
            if ((c == ' ') || (c == '\t') || (c == '\0') || (c == '\r') || (c == '\n'))
                break;

            int32_t chr = (unsigned char)c;
            TbBool WideChar = is_wide_charcode(chr);
            if (WideChar)
            {
                if (str[i + 1] == '\0') break;
                chr = (chr << 8) + (unsigned char)str[i + 1];
            }
            else if (str[i] == '\xc2' && str[i + 1] == '\xa0')
            {
                chr = (chr << 8) + (unsigned char)str[i + 1];
                WideChar = true;
            }

            if (WideChar)
            {
                if (len != 0) break;
                return static_cast<int32_t>(dbc_char_widthM(chr, units_per_px));
            }
            len += static_cast<int32_t>(dbc_char_widthM(chr, units_per_px));
        }
        return len;
    }

    return LbSprFontWordWidth(m_font, str) * units_per_px / 16;
}

int32_t SoftwareTextRenderer::TextHeight(const char* text)
{
    return LineHeight();
}

int32_t SoftwareTextRenderer::StringHeight(int32_t units_per_px, const char* text)
{
    if (!m_font || !text)
        return 0;

    int32_t nlines = 0;
    int32_t lnwidth_clip = m_justify_window.x - m_clip_window.x;
    int32_t lnwidth = lnwidth_clip;

    for (const char* pchr = text; *pchr != '\0'; pchr++)
    {
        int32_t chr = (unsigned char)(*pchr);
        if (is_wide_charcode(chr))
        {
            pchr++;
            if (*pchr == '\0') break;
            chr = (chr << 8) + (unsigned char)*pchr;
        }

        if (chr > 32)
        {
            int32_t w = CharWidthScaled(chr, units_per_px);
            if (lnwidth + w - lnwidth_clip > m_justify_window.width)
            {
                lnwidth = lnwidth_clip + w;
                nlines++;
            }
            else
            {
                lnwidth += w;
            }
        }
        else if (chr == ' ')
        {
            if (lnwidth > 0)
            {
                int32_t w = CharWidth(' ') * units_per_px / 16;
                if (lnwidth + w + WordWidth(pchr + 1) * units_per_px / 16 - lnwidth_clip > m_justify_window.width)
                {
                    lnwidth = lnwidth_clip;
                    nlines++;
                }
                else
                {
                    lnwidth += w;
                }
            }
        }
        else
        {
            switch (chr)
            {
            case '\r':
                lnwidth = lnwidth_clip;
                nlines++;
                if (pchr[1] == '\n') pchr++;
                break;
            case '\n':
                lnwidth = lnwidth_clip;
                nlines++;
                break;
            case '\t':
            {
                int32_t w = CharWidth(' ') * units_per_px / 16;
                lnwidth += LbTextGetSpacesPerTab() * w;
                if (lnwidth + WordWidth(pchr + 1) * units_per_px / 16 - lnwidth_clip > m_justify_window.width)
                {
                    lnwidth = lnwidth_clip;
                    nlines++;
                }
                break;
            }
            case 14:
                pchr++;
                break;
            }
        }
    }
    nlines++;
    return nlines * (LineHeight() * units_per_px / 16);
}

/******************************************************************************/
/* Drawing                                                                    */
/******************************************************************************/

TextLayoutContext SoftwareTextRenderer::BuildLayoutContext() const
{
    TextLayoutContext ctx{};
    ctx.font           = m_font;
    ctx.dbc_font       = m_dbc_font;
    ctx.dbc_enabled    = m_dbc_enabled;
    ctx.draw_flags     = m_text_draw_flags;
    ctx.justify_window = m_justify_window;
    ctx.clip_window    = m_clip_window;
    ctx.spaces_per_tab = LbTextGetSpacesPerTab();
    return ctx;
}

void SoftwareTextRenderer::SwDrawSegment(const char* sbuf, const char* ebuf,
                                         int32_t x, int32_t y, int32_t space_len,
                                         int32_t units_per_px, void* userdata)
{
    auto* self = static_cast<SoftwareTextRenderer*>(userdata);
    self->PutDownSprites(sbuf, ebuf, x, y, space_len, units_per_px);
}

TbBool SoftwareTextRenderer::DrawTextResized(int32_t posx, int32_t posy,
                                             int32_t units_per_px, const char* text,
                                             TbDrawFlagsMask draw_flags)
{
    if (!m_font || !text)
        return true;

    if (m_write_cmds) { AppendTextCmd(posx, posy, units_per_px, /*absolute=*/false, text, draw_flags); return true; }

    m_text_draw_flags  = draw_flags;

    TbGraphicsWindow grwnd;
    RendererStoreViewport(&grwnd);

    // Load the clip window into the graphics window state so put_down functions
    // write to the correct region of lbDisplay.WScreen.
    TbGraphicsWindow clip_grwnd;
    clip_grwnd.x      = m_clip_window.x;
    clip_grwnd.y      = m_clip_window.y;
    clip_grwnd.width  = m_clip_window.width;
    clip_grwnd.height = m_clip_window.height;
    clip_grwnd.ptr    = nullptr;
    RendererLoadViewport(&clip_grwnd);

    TextLayoutContext ctx = BuildLayoutContext();
    TextLayout(ctx, posx, posy, units_per_px, text, SwDrawSegment, this);

    RendererLoadViewport(&grwnd);
    return true;
}

TbBool SoftwareTextRenderer::DrawTextAt(int32_t screen_x, int32_t screen_y,
                                        int32_t units_per_px, const char* text,
                                        TbDrawFlagsMask draw_flags)
{
    if (!m_font || !text)
        return true;

    if (m_write_cmds) { AppendTextCmd(screen_x, screen_y, units_per_px, /*absolute=*/true, text, draw_flags); return true; }

    m_text_draw_flags  = draw_flags;

    TbGraphicsWindow grwnd;
    RendererStoreViewport(&grwnd);

    TbGraphicsWindow clip_grwnd;
    clip_grwnd.x      = m_clip_window.x;
    clip_grwnd.y      = m_clip_window.y;
    clip_grwnd.width  = m_clip_window.width;
    clip_grwnd.height = m_clip_window.height;
    clip_grwnd.ptr    = nullptr;
    RendererLoadViewport(&clip_grwnd);

    int32_t space_w = CharWidthScaled(' ', units_per_px);
    PutDownSprites(text, text + strlen(text), screen_x, screen_y, space_w, units_per_px);

    RendererLoadViewport(&grwnd);
    return true;
}

/******************************************************************************/
/* Software IR deferral: snapshot + replay                                    */
/******************************************************************************/

void SoftwareTextRenderer::AppendTextCmd(int32_t x, int32_t y, int32_t units_per_px,
                                         bool absolute, const char* text,
                                         TbDrawFlagsMask draw_flags)
{
    IRTextDrawCmd cmd;
    cmd.pos_x        = x;
    cmd.pos_y        = y;
    cmd.units_per_px = units_per_px;
    cmd.absolute     = absolute ? 1 : 0;
    cmd.draw_colour = m_text_draw_colour;
    cmd.draw_flags   = draw_flags;
    cmd.justify_x    = m_justify_window.x;
    cmd.justify_y    = m_justify_window.y;
    cmd.justify_w    = m_justify_window.width;
    cmd.clip_x       = m_clip_window.x;
    cmd.clip_y       = m_clip_window.y;
    cmd.clip_w       = m_clip_window.width;
    cmd.clip_h       = m_clip_window.height;
    cmd.font         = m_font;
    cmd.dbc_font     = m_dbc_font;
    cmd.dbc_enabled  = m_dbc_enabled ? 1 : 0;
    cmd.dbc_colour0  = m_dbc_colour0;
    cmd.dbc_colour1  = m_dbc_colour1;
    cmd.font_generation = 0;   // same-frame replay; SW replay re-resolves via SetFont
    cmd.SetText(text);
    cmd.seq          = m_write_cmds->NextSeq();
    m_write_cmds->draws.Append(cmd);
}

void SoftwareTextRenderer::ReplayTextCommand(const IRTextDrawCmd& cmd)
{
    // Restore the captured state and draw immediately.  Detach the write buffer
    // first so DrawText* draw instead of re-appending.  SetFont re-derives the
    // DBC font/colours from the font identity (same within a frame).
    TextCommandBuffers* saved = m_write_cmds;
    m_write_cmds = nullptr;

    SetFont(reinterpret_cast<const struct TbSpriteSheet*>(cmd.font));
    m_justify_window = { cmd.justify_x, cmd.justify_y, cmd.justify_w, 0 };
    m_clip_window    = { cmd.clip_x, cmd.clip_y, cmd.clip_w, cmd.clip_h };
    m_text_draw_colour = cmd.draw_colour;

    if (cmd.absolute)
        DrawTextAt(cmd.pos_x, cmd.pos_y, cmd.units_per_px, cmd.text, cmd.draw_flags);
    else
        DrawTextResized(cmd.pos_x, cmd.pos_y, cmd.units_per_px, cmd.text, cmd.draw_flags);

    m_write_cmds = saved;
}

/******************************************************************************/
/* Sprite drawing (moved from bflib_sprfnt.c)                                 */
/******************************************************************************/

void SoftwareTextRenderer::PutDownSprites(const char* sbuf, const char* ebuf,
                                          int32_t x, int32_t y, int32_t len,
                                          int32_t units_per_px)
{
    if (units_per_px == 16)
    {
        if (m_dbc_enabled)
            PutDownDbcSprites(sbuf, ebuf, x, y, len);
        else
            PutDownSimpleSprites(sbuf, ebuf, x, y, len);
    }
    else
    {
        if (m_dbc_enabled)
            PutDownDbcSpritesResized(sbuf, ebuf, x, y, len, units_per_px);
        else
            PutDownSimpleSpritesResized(sbuf, ebuf, x, y, len, units_per_px);
    }
}

void SoftwareTextRenderer::PutDownSimpleSprites(const char* sbuf, const char* ebuf,
                                                int32_t x, int32_t y, int32_t len)
{
    for (const char* c = sbuf; c < ebuf; c++)
    {
        unsigned char chr = (unsigned char)(*c);
        if (c[0] == '\xc2' && c + 1 < ebuf && c[1] == '\xa0')
        {
            int32_t w = len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight();
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
            c++;
        }
        else if (chr == ' ')
        {
            int32_t w = len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight();
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else if (chr >= 15)
        {
            const struct TbSprite* spr = LbFontCharSprite(m_font, chr);
            if (spr != nullptr)
            {
                if ((m_text_draw_flags & Lb_TEXT_ONE_COLOR) != 0)
                    LbSpriteDrawOneColour(x, y, spr, m_text_draw_colour, m_text_draw_flags);
                else
                    LbSpriteDraw(x, y, spr, m_text_draw_flags);
                int32_t w = spr->SWidth;
                if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
                {
                    int32_t h = LineHeight();
                    LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
                }
                x += w;
            }
        }
        else if (chr == '\t')
        {
            int32_t w = len * (int32_t)LbTextGetSpacesPerTab();
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight();
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else
        {
            switch (chr)
            {
            case 1:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR4;  break;
            case 2:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR8;  break;
            case 3:  m_text_draw_flags ^= Lb_SPRITE_OUTLINE;    break;
            case 4:  m_text_draw_flags ^= Lb_SPRITE_FLIP_HORIZ; break;
            case 5:  m_text_draw_flags ^= Lb_SPRITE_FLIP_VERTIC; break;
            case 11: m_text_draw_flags ^= Lb_TEXT_UNDERLINE;     break;
            case 12: m_text_draw_flags ^= Lb_TEXT_ONE_COLOR;     break;
            case 14: c++; m_text_draw_colour = (unsigned char)(*c); break;
            default: break;
            }
        }
    }
}

void SoftwareTextRenderer::PutDownSimpleSpritesResized(const char* sbuf, const char* ebuf,
                                                       int32_t x, int32_t y,
                                                       int32_t space_len, int32_t units_per_px)
{
    for (const char* c = sbuf; c < ebuf; c++)
    {
        unsigned char chr = (unsigned char)(*c);
        if (c[0] == '\xc2' && c + 1 < ebuf && c[1] == '\xa0')
        {
            int32_t w = space_len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight() * units_per_px / 16;
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
            c++;
        }
        else if (chr == ' ')
        {
            int32_t w = space_len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight() * units_per_px / 16;
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else if (chr >= 15)
        {
            const struct TbSprite* spr = LbFontCharSprite(m_font, chr);
            if (spr != nullptr)
            {
                long dw = ((long)spr->SWidth  * units_per_px + 8) / 16;
                long dh = ((long)spr->SHeight * units_per_px + 8) / 16;
                if ((m_text_draw_flags & Lb_TEXT_ONE_COLOR) != 0)
                    LbSpriteDrawScaledOneColour(x, y, spr, dw, dh, m_text_draw_colour, m_text_draw_flags);
                else
                    LbSpriteDrawScaled(x, y, spr, dw, dh, m_text_draw_flags);
                int32_t w = spr->SWidth * units_per_px / 16;
                if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
                {
                    int32_t h = LineHeight() * units_per_px / 16;
                    LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
                }
                x += w;
            }
        }
        else if (chr == '\t')
        {
            int32_t w = space_len * (int32_t)LbTextGetSpacesPerTab();
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = LineHeight() * units_per_px / 16;
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else
        {
            switch (chr)
            {
            case 1:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR4;  break;
            case 2:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR8;  break;
            case 3:  m_text_draw_flags ^= Lb_SPRITE_OUTLINE;    break;
            case 4:  m_text_draw_flags ^= Lb_SPRITE_FLIP_HORIZ; break;
            case 5:  m_text_draw_flags ^= Lb_SPRITE_FLIP_VERTIC; break;
            case 11: m_text_draw_flags ^= Lb_TEXT_UNDERLINE;     break;
            case 12: m_text_draw_flags ^= Lb_TEXT_ONE_COLOR;     break;
            case 14: c++; m_text_draw_colour = (unsigned char)(*c); break;
            default: break;
            }
        }
    }
}

void SoftwareTextRenderer::PutDownDbcSprites(const char* sbuf, const char* ebuf,
                                             int32_t x, int32_t y, int32_t len)
{
    struct AsianFontWindow awind;
    awind.buf_ptr = RendererGetGraphicsWindowPtr();
    awind.width = RendererGraphicsWindowWidth();
    awind.height = RendererGraphicsWindowHeight();
    awind.scanline = RendererScreenWidth();
    TbBool needs_draw = false;
    unsigned long chr = 0;

    for (const char* c = sbuf; c < ebuf; c++)
    {
        chr = (unsigned char)(*c);
        if (is_wide_charcode(chr))
        {
            c++;
            chr = (chr << 8) | (unsigned char)(*c);
            needs_draw = true;
        }
        else if (chr > 32)
        {
            needs_draw = true;
        }
        else if (chr == ' ')
        {
            int32_t w = len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = static_cast<int32_t>(dbc_char_height(' '));
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else if (chr == '\t')
        {
            int32_t w = len * (int32_t)LbTextGetSpacesPerTab();
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = static_cast<int32_t>(dbc_char_height(' '));
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else
        {
            switch (chr)
            {
            case 1:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR4;  break;
            case 2:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR8;  break;
            case 3:  m_text_draw_flags ^= Lb_SPRITE_OUTLINE;    break;
            case 4:  m_text_draw_flags ^= Lb_SPRITE_FLIP_HORIZ; break;
            case 5:  m_text_draw_flags ^= Lb_SPRITE_FLIP_VERTIC; break;
            case 11: m_text_draw_flags ^= Lb_TEXT_UNDERLINE;     break;
            case 12: m_text_draw_flags ^= Lb_TEXT_ONE_COLOR;     break;
            case 14: c++; m_text_draw_colour = (unsigned char)(*c); break;
            default: break;
            }
        }

        if (needs_draw)
        {
            struct AsianDraw adraw;
            if (dbc_get_sprite_for_char(&adraw, chr) == 0)
            {
                unsigned long colour;
                if ((m_text_draw_flags & Lb_TEXT_ONE_COLOR) == 0)
                    colour = m_dbc_colour0;
                else
                    colour = m_text_draw_colour;
                dbc_draw_font_sprite_text(&awind, &adraw, x, y, colour, -1, m_dbc_colour1);
                int32_t w = adraw.character_spacing + adraw.bits_width;
                if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
                {
                    int32_t h = adraw.bits_height;
                    LbDrawCharUnderline(x, y, w, h, colour, m_text_shadow_colour, m_text_draw_flags);
                }
                x += w;
                if (x >= awind.width)
                    return;
            }
            needs_draw = false;
        }
    }
}

void SoftwareTextRenderer::PutDownDbcSpritesResized(const char* sbuf, const char* ebuf,
                                                    int32_t x, int32_t y,
                                                    int32_t space_len, int32_t units_per_px)
{
    struct AsianFontWindow awind;
    awind.buf_ptr = RendererGetGraphicsWindowPtr();
    awind.width = RendererGraphicsWindowWidth();
    awind.height = RendererGraphicsWindowHeight();
    awind.scanline = RendererScreenWidth();
    TbBool needs_draw = false;
    unsigned long chr = 0;

    for (const char* c = sbuf; c < ebuf; c++)
    {
        chr = (unsigned char)(*c);
        if (is_wide_charcode(chr))
        {
            c++;
            chr = (chr << 8) | (unsigned char)(*c);
            needs_draw = true;
        }
        else if (chr > 32)
        {
            needs_draw = true;
        }
        else if (chr == ' ')
        {
            int32_t w = space_len;
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = static_cast<int32_t>(dbc_char_height(' ')) * units_per_px / 16;
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else if (chr == '\t')
        {
            int32_t w = space_len * (int32_t)LbTextGetSpacesPerTab();
            if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
            {
                int32_t h = static_cast<int32_t>(dbc_char_height(' ')) * units_per_px / 16;
                LbDrawCharUnderline(x, y, w, h, m_text_draw_colour, m_text_shadow_colour, m_text_draw_flags);
            }
            x += w;
        }
        else
        {
            switch (chr)
            {
            case 1:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR4;  break;
            case 2:  m_text_draw_flags ^= Lb_SPRITE_TRANSPAR8;  break;
            case 3:  m_text_draw_flags ^= Lb_SPRITE_OUTLINE;    break;
            case 4:  m_text_draw_flags ^= Lb_SPRITE_FLIP_HORIZ; break;
            case 5:  m_text_draw_flags ^= Lb_SPRITE_FLIP_VERTIC; break;
            case 11: m_text_draw_flags ^= Lb_TEXT_UNDERLINE;     break;
            case 12: m_text_draw_flags ^= Lb_TEXT_ONE_COLOR;     break;
            case 14: c++; m_text_draw_colour = (unsigned char)(*c); break;
            default: break;
            }
        }

        if (needs_draw)
        {
            struct AsianDraw adraw;
            if (dbc_get_sprite_for_char(&adraw, chr) == 0)
            {
                unsigned long colour;
                if ((m_text_draw_flags & Lb_TEXT_ONE_COLOR) == 0)
                    colour = m_dbc_colour0;
                else
                    colour = m_text_draw_colour;

                unsigned char dest_pixel[1024] = { 0 };
                int32_t iDstSizeH = (units_per_px / 8) * 8;
                int32_t iDstSizeW = iDstSizeH;
                if (!is_wide_charcode(chr))
                    iDstSizeW -= (8 * (iDstSizeW / 16));

                float scale_factorX = (float)adraw.bits_width / (float)iDstSizeW;
                float scale_factorY = (float)adraw.bits_height / (float)iDstSizeH;
                for (int sY = 0; sY < iDstSizeH; sY++)
                {
                    for (int sX = 0; sX < iDstSizeW; sX++)
                    {
                        set_bit_to_array(dest_pixel, sX, sY, iDstSizeW,
                            get_bit_to_array(adraw.sprite_data, (int)(sX * scale_factorX),
                                             (int)(sY * scale_factorY), adraw.bits_width));
                    }
                }

                adraw.bits_width = iDstSizeW;
                adraw.bits_height = iDstSizeH;
                adraw.sprite_data = dest_pixel;

                dbc_draw_font_sprite_text(&awind, &adraw, x, y, colour, -1, m_dbc_colour1);

                int32_t w;
                if (adraw.bits_height == 16)
                    w = (adraw.character_spacing + adraw.bits_width) * units_per_px / 16;
                else
                    w = (adraw.character_spacing + adraw.bits_width);

                if ((m_text_draw_flags & Lb_TEXT_UNDERLINE) != 0)
                {
                    int32_t h = adraw.bits_height * units_per_px / 16;
                    LbDrawCharUnderline(x, y, w, h, colour, m_text_shadow_colour, m_text_draw_flags);
                }
                x += w;
                if (x >= awind.width)
                    return;
            }
            needs_draw = false;
        }
    }
}
