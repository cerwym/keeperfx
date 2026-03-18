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

/******************************************************************************/

/**
 * CPU software implementation of ITextRenderer.
 *
 * Calls the original sprite font rasterizer directly:
 *   DrawTextResized  →  LbTextDrawResized_sw(posx, posy, units_per_px, text)
 */
class SoftwareTextRenderer : public ITextRenderer {
public:
    SoftwareTextRenderer()  = default;
    ~SoftwareTextRenderer() = default;

    TbBool DrawTextResized(int posx, int posy, int units_per_px, const char* text) override;

    const char* GetName() const override { return "SOFTWARE"; }
};

/******************************************************************************/
#endif // SOFTWARE_TEXT_RENDERER_H
