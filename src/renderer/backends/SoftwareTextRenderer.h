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
    TbBool DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text, TbDrawFlagsMask draw_flags) override;
    TbBool DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text, TbDrawFlagsMask draw_flags) override;
    void   SetDrawColour(uint8_t colour) override { m_text_draw_colour = colour; }
    uint8_t GetDrawColour() const override        { return m_text_draw_colour; }
    void   SetShadowColour(uint8_t colour) override { m_text_shadow_colour = colour; }
    uint8_t GetShadowColour() const override        { return m_text_shadow_colour; }

    // ── Software IR executor ──────────────────────────────────────────────────
    /** Open/close the text IR write window (software deferral). When set, the
     *  DrawText* calls snapshot into an IRTextDrawCmd instead of drawing. */
    void SetTextCommandBuffers(struct TextCommandBuffers* cmds) override { m_write_cmds = cmds; }
    /** Restore the captured text state and draw immediately — called by the
     *  merged seq-ordered replay in IUIRenderer::ReplayMergedFromIR. */
    void ReplayTextCommand(const struct IRTextDrawCmd& cmd) override;

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

    /** Snapshot current font/window/draw state into an IRTextDrawCmd and append
     *  it to m_write_cmds (software deferral).  Stamps the shared seq. */
    void AppendTextCmd(int32_t x, int32_t y, int32_t units_per_px,
                       bool absolute, const char* text,
                       TbDrawFlagsMask draw_flags);

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
    /* Working draw-state during layout — owned here, seeded at draw entry,    */
    /* mutated by inline text escape codes.                                    */
    /**************************************************************************/
    unsigned int                m_text_draw_flags  = 0;
    unsigned char               m_text_draw_colour = 0;
    unsigned char               m_text_shadow_colour = 0;

    /**************************************************************************/
    /* Text windows                                                           */
    /**************************************************************************/
    TextWindow                  m_justify_window;
    TextWindow                  m_clip_window;

    /**************************************************************************/
    /* Software IR deferral                                                    */
    /**************************************************************************/
    /** When non-null, DrawText* append an IRTextDrawCmd instead of drawing. */
    struct TextCommandBuffers*  m_write_cmds = nullptr;
};

/******************************************************************************/
