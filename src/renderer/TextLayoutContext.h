/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file TextLayoutContext.h
 *     Shared types for the text layout engine and ITextRenderer implementations.
 */
/******************************************************************************/
#ifndef TEXT_LAYOUT_CONTEXT_H
#define TEXT_LAYOUT_CONTEXT_H

#include <stdint.h>

struct TbSpriteSheet;
struct AsianFont;

/******************************************************************************/

/** Rectangular region used for text justify and clip operations. */
struct TextWindow {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
};

/** Callback invoked by TextLayout() once per justified line segment.
 *  @param sbuf        Start of text segment (inclusive)
 *  @param ebuf        End of text segment (exclusive)
 *  @param x           X position in clip-window-relative coordinates
 *  @param y           Y position in clip-window-relative coordinates
 *  @param space_len   Pixel width of a space for this segment (may be justify-expanded)
 *  @param units_per_px Scale factor (16 = 100%)
 *  @param userdata    Caller-supplied context pointer */
typedef void (*TextSegmentFn)(const char* sbuf, const char* ebuf,
                              int32_t x, int32_t y, int32_t space_len,
                              int32_t units_per_px, void* userdata);

/** Snapshot of all state the paragraph layout engine needs.
 *  Built by each ITextRenderer from its own member state; passed to TextLayout()
 *  so the layout engine never reads globals. */
struct TextLayoutContext {
    const struct TbSpriteSheet* font;
    const struct AsianFont*     dbc_font;
    int8_t                      dbc_enabled;
    uint16_t                    draw_flags;
    TextWindow                  justify_window;
    TextWindow                  clip_window;
    uint32_t                    spaces_per_tab;
};

/******************************************************************************/
#endif // TEXT_LAYOUT_CONTEXT_H
