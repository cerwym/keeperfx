/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareTextRenderer.h
 *     CPU software implementation of ITextRenderer.
 */
/******************************************************************************/
#pragma once

#include "renderer/ITextRenderer.h"
#include "renderer/TextLayoutContext.h"
#include <stdint.h>

struct TbSpriteSheet;
struct AsianFont;
struct AsianFontWindow;
struct AsianDraw;

/******************************************************************************/

class SoftwareTextRenderer : public ITextRenderer {
public:
    SoftwareTextRenderer();
    ~SoftwareTextRenderer() = default;

    // Font
    void SetFont(const struct TbSpriteSheet* font) override;
    const struct TbSpriteSheet* GetFont() const override { return m_font; }

    // Windowing
    void SetWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void SetJustifyWindow(int32_t x, int32_t y, int32_t w) override;
    void SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const override;
    void GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const override;

    // Drawing
    TbBool DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text) override;
    TbBool DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text) override;

    // Measurement
    int32_t LineHeight() override;
    int32_t CharWidth(uint32_t chr) override;
    int32_t CharWidthScaled(uint32_t chr, int32_t units_per_px) override;
    int32_t StringWidth(const char* text) override;
    int32_t StringWidthScaled(const char* text, int32_t units_per_px) override;
    int32_t WordWidth(const char* str) override;
    int32_t WordWidthScaled(const char* str, int32_t units_per_px) override;
    int32_t TextHeight(const char* text) override;
    int32_t StringHeight(int32_t units_per_px, const char* text) override;

    const char* GetName() const override { return "SOFTWARE"; }

private:
    /** Build a TextLayoutContext snapshot from current member state. */
    TextLayoutContext BuildLayoutContext() const;

    /** Segment callback for paragraph layout — dispatches to PutDownSprites. */
    static void SwDrawSegment(const char* sbuf, const char* ebuf,
                              int32_t x, int32_t y, int32_t space_len,
                              int32_t units_per_px, void* userdata);

    /** Top-level sprite dispatcher: routes to simple or DBC, scaled or unscaled. */
    void PutDownSprites(const char* sbuf, const char* ebuf,
                        int32_t x, int32_t y, int32_t len, int32_t units_per_px);

    void PutDownSimpleSprites(const char* sbuf, const char* ebuf,
                              int32_t x, int32_t y, int32_t len);
    void PutDownSimpleSpritesResized(const char* sbuf, const char* ebuf,
                                     int32_t x, int32_t y, int32_t space_len,
                                     int32_t units_per_px);
    void PutDownDbcSprites(const char* sbuf, const char* ebuf,
                           int32_t x, int32_t y, int32_t len);
    void PutDownDbcSpritesResized(const char* sbuf, const char* ebuf,
                                  int32_t x, int32_t y, int32_t space_len,
                                  int32_t units_per_px);

    /**************************************************************************/
    /* Font / DBC state                                                       */
    /**************************************************************************/
    const struct TbSpriteSheet* m_font;
    const struct AsianFont*     m_dbc_font;
    int32_t                     m_dbc_colour0;
    int32_t                     m_dbc_colour1;
    TbBool                      m_dbc_enabled;

    /**************************************************************************/
    /* Text windows                                                           */
    /**************************************************************************/
    TextWindow                  m_justify_window;
    TextWindow                  m_clip_window;
};

/******************************************************************************/
