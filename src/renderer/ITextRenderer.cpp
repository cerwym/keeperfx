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
#include "bflib_video.h"     // Lb_TEXT_HALIGN_*
#include "post_inc.h"

/******************************************************************************/

/** DK text control codes embedded in strings (values < 15). */
enum TextControlCode : int32_t {
    TCC_ALIGN_LEFT    = 6,   // toggle left alignment
    TCC_ALIGN_RIGHT   = 7,   // toggle right alignment
    TCC_ALIGN_CENTER  = 8,   // toggle centre alignment
    TCC_ALIGN_JUSTIFY = 9,   // toggle full justification (NOTE: same value as '\t',
                             // so tab handling takes priority in the switch below)
    TCC_ESCAPE        = 14,  // skip next byte (colour/style escape)
};

void ITextRenderer::TextLayout(const TextLayoutContext& ctx,
                               int32_t posx, int32_t posy,
                               int32_t units_per_px,
                               const char* text,
                               TextSegmentFn draw_fn, void* userdata)
{
    if (!ctx.font || !text)
        return;

    uint16_t flags = ctx.draw_flags;

    const int32_t justifyx = ctx.justify_window.x - ctx.clip_window.x;
    const int32_t justifyy = ctx.justify_window.y - ctx.clip_window.y;
    posx += justifyx;
    const int32_t startx = posx;
    int32_t starty = posy + justifyy;
    const int32_t h = LineHeight() * units_per_px / 16;
    const char* sbuf = text;
    int32_t count = 0;

    // Helper: emit the accumulated segment, then advance to the next line.
    auto emit_line = [&](const char* seg_end, int32_t spr_w, int32_t mul_w) {
        int32_t x = JustifiedCharPosX(startx, posx, spr_w, mul_w, flags, ctx.justify_window, ctx.clip_window);
        int32_t y = JustifiedCharPosY(starty, h, h, flags);
        int32_t len = JustifiedCharWidth(posx, spr_w, count, units_per_px, flags, ctx.justify_window, ctx.clip_window);
        draw_fn(sbuf, seg_end, x, y, len, units_per_px, userdata);
    };

    auto next_line = [&]() {
        posx    = startx;
        starty += h;
        count   = 0;
    };

    const char* ebuf;
    for (ebuf = text; *ebuf != '\0'; ebuf++)
    {
        const char* seg_break = ebuf;  // position just before this character
        int32_t chr = (unsigned char)*ebuf;

        // --- Multi-byte character decoding ---
        TbBool wide = is_wide_charcode(chr);
        if (wide)
        {
            ebuf++;
            if (*ebuf == '\0') break;
            chr = (chr << 8) + (unsigned char)*ebuf;
        }
        else if (ebuf[0] == '\xc2' && ebuf[1] == '\xa0')  // UTF-8 non-breaking space
        {
            ebuf++;
            chr = (chr << 8) + (unsigned char)*ebuf;
            wide = true;
        }

        // --- Dispatch on character type ---
        switch (chr)
        {
        case ' ':
        {
            int32_t w = CharWidthScaled(' ', units_per_px);
            int32_t wordw = WordWidthScaled(ebuf + 1, units_per_px);
            if (posx + w + wordw - justifyx <= ctx.justify_window.width)
            {
                count++;
                posx += w;
                continue;
            }
            posx += w;
            emit_line(ebuf, w, 1);
            if (AlignMethodSet(flags))
            {
                posx   = startx;
                sbuf   = ebuf + 1;
                starty += h;
            }
            count = 0;
            continue;
        }

        case '\n':
        {
            int32_t x = JustifiedCharPosX(startx, posx, 0, 1, flags, ctx.justify_window, ctx.clip_window);
            int32_t len = CharWidthScaled(' ', units_per_px);
            draw_fn(sbuf, ebuf, x, starty, len, units_per_px, userdata);
            sbuf = ebuf;
            next_line();
            continue;
        }

        case '\t':
        {
            int32_t w = CharWidthScaled(' ', units_per_px);
            posx += (int32_t)ctx.spaces_per_tab * w;
            int32_t wordw = WordWidthScaled(ebuf + 1, units_per_px);
            if (posx + wordw - justifyx <= ctx.justify_window.width)
            {
                count += ctx.spaces_per_tab;
                continue;
            }
            emit_line(ebuf, w, (int32_t)ctx.spaces_per_tab);
            if (AlignMethodSet(flags))
            {
                sbuf = ebuf + 1;
                next_line();
            }
            else
            {
                count = 0;
            }
            continue;
        }

        case TCC_ALIGN_LEFT:
        case TCC_ALIGN_RIGHT:
        case TCC_ALIGN_CENTER:
        // NOTE: TCC_ALIGN_JUSTIFY (9) shares the same value as '\t'.
        // Tab handling above takes priority, matching the original if/else-if
        // chain where the tab branch preceded the alignment branch.
        {
            if (posx - justifyx > ctx.justify_window.width)
            {
                int32_t len = CharWidthScaled(' ', units_per_px);
                draw_fn(sbuf, ebuf, startx, starty, len, units_per_px, userdata);
                sbuf = ebuf;
                next_line();
            }
            switch (chr)
            {
            case TCC_ALIGN_LEFT:    flags ^= Lb_TEXT_HALIGN_LEFT;    break;
            case TCC_ALIGN_RIGHT:   flags ^= Lb_TEXT_HALIGN_RIGHT;   break;
            case TCC_ALIGN_CENTER:  flags ^= Lb_TEXT_HALIGN_CENTER;  break;
            }
            continue;
        }

        case TCC_ESCAPE:
            ebuf++;
            if (*ebuf == '\0') break;
            continue;

        default:
            if (chr < 15)
                continue;  // other control codes (1-5, 11-13): silently consumed
            break;  // printable character — fall through to glyph handling below
        }

        if (*ebuf == '\0')
            break;  // TCC_ESCAPE hit end-of-string

        // --- Printable glyph (chr >= 15 and not a space/control) ---
        int32_t w = CharWidthScaled(chr, units_per_px);
        if (wide) count = 0;
        if ((posx + w - justifyx <= ctx.justify_window.width) || (count > 0) || !AlignMethodSet(flags))
        {
            posx += w;
            continue;
        }
        // Word-wrap: glyph doesn't fit — emit current segment and break to next line.
        w = CharWidthScaled(' ', units_per_px);
        posx += w;
        emit_line(seg_break, w, 1);
        sbuf  = seg_break;
        ebuf  = sbuf - 1;  // re-process this character on the new line
        next_line();
    }

    // Final segment — pass real space width so FlushSegment renders spaces correctly.
    emit_line(ebuf, CharWidthScaled(' ', units_per_px), 1);
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
