/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SoftwareTextRenderer.h
 *     CPU software implementation of ITextRenderer.
 */
/******************************************************************************/
#ifndef SOFTWARE_TEXT_RENDERER_H
#define SOFTWARE_TEXT_RENDERER_H

#include "renderer/ITextRenderer.h"
#include "renderer/TextLayoutContext.h"

struct TbSpriteSheet;
struct AsianFont;

/******************************************************************************/

/**
 * CPU software implementation of ITextRenderer.
 *
 * Phase 1: Delegates to existing bflib_sprfnt.c functions via globals.
 * Phase 2+: Will own font/window state and drawing routines internally.
 */
class SoftwareTextRenderer : public ITextRenderer {
public:
    SoftwareTextRenderer()  = default;
    ~SoftwareTextRenderer() = default;

    // Font
    void SetFont(const struct TbSpriteSheet* font) override;

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
};

/******************************************************************************/
#endif // SOFTWARE_TEXT_RENDERER_H
