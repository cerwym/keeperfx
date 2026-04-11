/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ITextRenderer.cpp
 *     Shared implementation for ITextRenderer protected helpers.
 *     TextLayout() and justification helpers — used by both SW and GL renderers.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/ITextRenderer.h"
#include "bflib_sprfnt.h"    // is_wide_charcode
#include "bflib_vidraw.h"    // Lb_TEXT_HALIGN_*
#include "post_inc.h"

/******************************************************************************/

void ITextRenderer::TextLayout(const TextLayoutContext& ctx,
                               int32_t posx, int32_t posy,
                               int32_t units_per_px,
                               const char* text,
                               TextSegmentFn draw_fn, void* userdata)
{
    if (!ctx.font || !text)
        return;

    // Local mutable copy — alignment control codes (6-9) toggle these
    uint16_t flags = ctx.draw_flags;

    int32_t justifyx = ctx.justify_window.x - ctx.clip_window.x;
    int32_t justifyy = ctx.justify_window.y - ctx.clip_window.y;
    posx += justifyx;
    int32_t startx = posx;
    int32_t starty = posy + justifyy;
    int32_t h = LineHeight() * units_per_px / 16;
    const char* sbuf = text;
    int32_t count = 0;

    const char* ebuf;
    for (ebuf = text; *ebuf != '\0'; ebuf++)
    {
        const char* text_backup_pointer = ebuf;
        int32_t chr = (unsigned char)*ebuf;
        TbBool WideChar = is_wide_charcode(chr);
        if (WideChar)
        {
            ebuf++;
            if (*ebuf == '\0') break;
            chr = (chr << 8) + (unsigned char)*ebuf;
        }
        else if (ebuf[0] == '\xc2' && ebuf[1] == '\xa0')
        {
            ebuf++;
            chr = (chr << 8) + (unsigned char)*ebuf;
            WideChar = true;
        }

        int32_t w;
        if ((chr >= 15) && (chr != 32))
        {
            w = CharWidthScaled(chr, units_per_px);
            if (WideChar) count = 0;
            if ((posx + w - justifyx <= ctx.justify_window.width) || (count > 0) || !AlignMethodSet(flags))
            {
                posx += w;
                continue;
            }
            w = CharWidthScaled(' ', units_per_px);
            posx += w;
            int32_t x = JustifiedCharPosX(startx, posx, w, 1, flags, ctx.justify_window, ctx.clip_window);
            int32_t y = JustifiedCharPosY(starty, h, h, flags);
            int32_t len = JustifiedCharWidth(posx, w, count, units_per_px, flags, ctx.justify_window, ctx.clip_window);
            draw_fn(sbuf, text_backup_pointer, x, y, len, units_per_px, userdata);
            posx   = startx;
            sbuf   = text_backup_pointer;
            ebuf   = sbuf - 1;
            starty += h;
            count  = 0;
        }
        else if (chr == ' ')
        {
            w = CharWidthScaled(' ', units_per_px);
            int32_t wordw = WordWidthScaled(ebuf + 1, units_per_px);
            if (posx + w + wordw - justifyx <= ctx.justify_window.width)
            {
                count++;
                posx += w;
                continue;
            }
            posx += w;
            int32_t x = JustifiedCharPosX(startx, posx, w, 1, flags, ctx.justify_window, ctx.clip_window);
            int32_t y = JustifiedCharPosY(starty, h, h, flags);
            int32_t len = JustifiedCharWidth(posx, w, count, units_per_px, flags, ctx.justify_window, ctx.clip_window);
            draw_fn(sbuf, ebuf, x, y, len, units_per_px, userdata);
            if (AlignMethodSet(flags))
            {
                posx   = startx;
                sbuf   = ebuf + 1;
                starty += h;
            }
            count = 0;
        }
        else if (chr == '\n')
        {
            int32_t x = JustifiedCharPosX(startx, posx, 0, 1, flags, ctx.justify_window, ctx.clip_window);
            int32_t y = starty;
            int32_t len = CharWidthScaled(' ', units_per_px);
            draw_fn(sbuf, ebuf, x, y, len, units_per_px, userdata);
            sbuf   = ebuf;
            posx   = startx;
            starty += h;
            count  = 0;
        }
        else if (chr == '\t')
        {
            w = CharWidthScaled(' ', units_per_px);
            posx += (int32_t)ctx.spaces_per_tab * w;
            int32_t wordw = WordWidthScaled(ebuf + 1, units_per_px);
            if (posx + wordw - justifyx <= ctx.justify_window.width)
            {
                count += ctx.spaces_per_tab;
                continue;
            }
            int32_t x = JustifiedCharPosX(startx, posx, w, (int32_t)ctx.spaces_per_tab, flags, ctx.justify_window, ctx.clip_window);
            int32_t y = JustifiedCharPosY(starty, h, h, flags);
            int32_t len = JustifiedCharWidth(posx, w, count, units_per_px, flags, ctx.justify_window, ctx.clip_window);
            draw_fn(sbuf, ebuf, x, y, len, units_per_px, userdata);
            if (AlignMethodSet(flags))
            {
                posx   = startx;
                sbuf   = ebuf + 1;
                starty += h;
            }
            count = 0;
            continue;
        }
        else if ((chr == 6) || (chr == 7) || (chr == 8) || (chr == 9))
        {
            if (posx - justifyx > ctx.justify_window.width)
            {
                int32_t x = startx;
                int32_t y = starty;
                int32_t len = CharWidthScaled(' ', units_per_px);
                draw_fn(sbuf, ebuf, x, y, len, units_per_px, userdata);
                posx   = startx;
                sbuf   = ebuf;
                count  = 0;
                starty += h;
            }
            switch (*ebuf)
            {
            case 6: flags ^= Lb_TEXT_HALIGN_LEFT;    break;
            case 7: flags ^= Lb_TEXT_HALIGN_RIGHT;   break;
            case 8: flags ^= Lb_TEXT_HALIGN_CENTER;  break;
            case 9: flags ^= Lb_TEXT_HALIGN_JUSTIFY; break;
            }
        }
        else if (chr == 14)
        {
            ebuf++;
            if (*ebuf == '\0') break;
        }
    }

    // Final segment
    int32_t x = JustifiedCharPosX(startx, posx, 0, 1, flags, ctx.justify_window, ctx.clip_window);
    int32_t y = JustifiedCharPosY(starty, h, h, flags);
    int32_t len = CharWidthScaled(' ', units_per_px);
    draw_fn(sbuf, ebuf, x, y, len, units_per_px, userdata);
}

/******************************************************************************/

bool ITextRenderer::AlignMethodSet(uint16_t flags)
{
    const uint16_t align_flags =
        Lb_TEXT_HALIGN_LEFT | Lb_TEXT_HALIGN_RIGHT
      | Lb_TEXT_HALIGN_CENTER | Lb_TEXT_HALIGN_JUSTIFY;
    return (flags & align_flags) != 0;
}

int32_t ITextRenderer::JustifiedCharPosX(int32_t startx, int32_t all_width,
                                         int32_t spr_width, int32_t mul_width,
                                         uint16_t flags,
                                         const TextWindow& justify,
                                         const TextWindow& clip)
{
    if ((flags & Lb_TEXT_HALIGN_LEFT) != 0)
        return startx;

    int32_t justifyx = justify.x - clip.x;

    if ((flags & Lb_TEXT_HALIGN_RIGHT) != 0)
        return startx + (justify.width + justifyx + mul_width * spr_width - all_width);

    if ((flags & Lb_TEXT_HALIGN_CENTER) != 0)
        return startx + (justify.width + justifyx + mul_width * spr_width - all_width) / 2;

    return startx;
}

int32_t ITextRenderer::JustifiedCharPosY(int32_t starty, int32_t /*all_height*/,
                                         int32_t /*spr_height*/, uint16_t /*flags*/)
{
    return starty;
}

int32_t ITextRenderer::JustifiedCharWidth(int32_t all_width, int32_t spr_width,
                                          int32_t word_count, int32_t units_per_px,
                                          uint16_t flags,
                                          const TextWindow& justify,
                                          const TextWindow& clip)
{
    if ((flags & Lb_TEXT_HALIGN_JUSTIFY) != 0)
    {
        // spr_width is the space char width — callers always pass CharWidthScaled(' ', ups)
        int32_t justifyx = justify.x - clip.x;
        if (word_count > 0)
            return spr_width + (justify.width + justifyx + spr_width - all_width) / word_count;
        return spr_width;
    }
    return spr_width;
}
