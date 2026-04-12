/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ITextRenderer.h
 *     Abstract interface for bitmap text rendering.
 * @par Design:
 *     Game code calls LbText*() shims in bflib_sprfnt.c, which dispatch
 *     through TextRenderer_*() C bridges in RendererManager.cpp to the
 *     active ITextRenderer implementation.
 *
 *     The interface owns all text-related state: active font (Western + DBC),
 *     text windows (justify/clip), measurement, layout, and rendering.
 *     No font or window globals exist outside the renderer.
 *
 *     Two drawing modes:
 *       - DrawTextResized(): renders relative to the current text window
 *         with word-wrap and justification via the shared TextLayout() engine.
 *       - DrawTextAt(): renders at absolute screen coordinates with no
 *         window setup required — single line, direct placement.
 */
/******************************************************************************/
#ifndef ITEXT_RENDERER_H
#define ITEXT_RENDERER_H

#include "bflib_basics.h"
#include "renderer/TextLayoutContext.h"
#include <stdint.h>

struct TbSpriteSheet;
struct AsianFont;

#ifdef __cplusplus
/******************************************************************************/

class ITextRenderer {
public:
    virtual ~ITextRenderer() = default;

    /**************************************************************************/
    /* Font                                                                   */
    /**************************************************************************/

    /** Set the active font for subsequent measurement and drawing calls.
     *  Resolves both the Western sprite font and the matching DBC (Asian)
     *  font internally.  Does not destroy previously used font data. */
    virtual void SetFont(const struct TbSpriteSheet* font) = 0;

    /** Return the active Western font, or NULL if none has been set. */
    virtual const struct TbSpriteSheet* GetFont() const = 0;

    /**************************************************************************/
    /* Windowing                                                              */
    /**************************************************************************/

    /** Set both the justify and clip windows to the same rectangle.
     *  Equivalent to SetJustifyWindow(x,y,w) + SetClipWindow(x,y,w,h). */
    virtual void SetWindow(int32_t x, int32_t y, int32_t w, int32_t h) = 0;

    /** Set the justify window (origin + width for text wrapping). */
    virtual void SetJustifyWindow(int32_t x, int32_t y, int32_t w) = 0;

    /** Set the clip window (visible rectangle for text clipping).
     *  Coordinates are clamped to screen bounds. */
    virtual void SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h) = 0;

    /** Query the current justify window. Any pointer may be NULL. */
    virtual void GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const = 0;

    /** Query the current clip window. Any pointer may be NULL. */
    virtual void GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const = 0;

    /**************************************************************************/
    /* Drawing                                                                */
    /**************************************************************************/

    /** Draw text at (posx, posy) relative to the current text window,
     *  scaled by units_per_px.  units_per_px == 16 means 100%.
     *  Applies word-wrap and justification via the layout engine.
     *  GPU backends may defer the actual draw until Flush(). */
    virtual TbBool DrawTextResized(int32_t posx, int32_t posy,
                                   int32_t units_per_px, const char* text) = 0;

    /** Draw text at absolute screen coordinates.  No text window setup needed.
     *  Single line, no word-wrap, no justification.
     *  GPU backends may defer the actual draw until Flush(). */
    virtual TbBool DrawTextAt(int32_t screen_x, int32_t screen_y,
                              int32_t units_per_px, const char* text) = 0;

    /** Flush any deferred text draws to the framebuffer.
     *  Called at end-of-frame after the staging-buffer blit.
     *  Software backends may leave this as a no-op. */
    virtual void Flush() {}

    /**************************************************************************/
    /* Measurement                                                            */
    /**************************************************************************/

    /** Height of one line of text in the current font (unscaled). */
    virtual int32_t LineHeight() = 0;

    /** Width of a single character (unscaled). */
    virtual int32_t CharWidth(uint32_t chr) = 0;

    /** Width of a single character (scaled by units_per_px). */
    virtual int32_t CharWidthScaled(uint32_t chr, int32_t units_per_px) = 0;

    /** Width of a complete string (unscaled). */
    virtual int32_t StringWidth(const char* text) = 0;

    /** Width of a complete string (scaled by units_per_px). */
    virtual int32_t StringWidthScaled(const char* text, int32_t units_per_px) = 0;

    /** Width of the next word in the string (unscaled). */
    virtual int32_t WordWidth(const char* str) = 0;

    /** Width of the next word in the string (scaled by units_per_px). */
    virtual int32_t WordWidthScaled(const char* str, int32_t units_per_px) = 0;

    /** Height of a string (accounts for newlines, unscaled). */
    virtual int32_t TextHeight(const char* text) = 0;

    /** Height a string would occupy if drawn with word-wrap at the given scale. */
    virtual int32_t StringHeight(int32_t units_per_px, const char* text) = 0;

    /**************************************************************************/
    /* Identity                                                               */
    /**************************************************************************/

    virtual const char* GetName() const = 0;

protected:
    /** Shared paragraph layout engine.
     *  Walks the text string applying word-wrap and justification against
     *  ctx.justify_window, calling draw_fn once for each justified line segment.
     *  Coordinates passed to draw_fn are clip-window-relative.
     *  Both SW and GL renderers call this with their own callbacks. */
    void TextLayout(const TextLayoutContext& ctx,
                    int32_t posx, int32_t posy, int32_t units_per_px,
                    const char* text, TextSegmentFn draw_fn, void* userdata);

    /**************************************************************************/
    /* Justification helpers (used by TextLayout and renderers)               */
    /**************************************************************************/

    static bool AlignMethodSet(uint16_t flags);
    static int32_t JustifiedCharPosX(int32_t startx, int32_t all_width,
                                     int32_t spr_width, int32_t mul_width,
                                     uint16_t flags,
                                     const TextWindow& justify,
                                     const TextWindow& clip);
    static int32_t JustifiedCharPosY(int32_t starty, int32_t all_height,
                                     int32_t spr_height, uint16_t flags);
    static int32_t JustifiedCharWidth(int32_t all_width, int32_t spr_width,
                                      int32_t word_count, int32_t units_per_px,
                                      uint16_t flags,
                                      const TextWindow& justify,
                                      const TextWindow& clip);
};

/******************************************************************************/
#endif // __cplusplus
#endif // ITEXT_RENDERER_H
