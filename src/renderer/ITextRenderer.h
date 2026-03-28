/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ITextRenderer.h
 *     Abstract interface for bitmap text rendering.
 * @par Design:
 *     Game code calls LbTextDrawResized() shims in bflib_sprfnt.c, which
 *     dispatch here. The software backend calls LbTextDrawResized_sw() directly.
 *
 *     A GPU backend would upload glyphs to a texture atlas and batch draw calls.
 */
/******************************************************************************/
#ifndef ITEXT_RENDERER_H
#define ITEXT_RENDERER_H

#include "bflib_basics.h"

#ifdef __cplusplus
/******************************************************************************/

class ITextRenderer {
public:
    virtual ~ITextRenderer() = default;

    /**
     * Draw text at (posx, posy) scaled by units_per_px.
     * units_per_px == 16 means 100% (1 source pixel = 1 screen pixel).
     * GPU backends may defer the actual draw until Flush() is called.
     */
    virtual TbBool DrawTextResized(int posx, int posy, int units_per_px, const char* text) = 0;

    /**
     * Flush any deferred text draws to the framebuffer.
     * Called by the renderer backend at the end of each frame, after the
     * staging-buffer blit quad and before the buffer swap.
     * Software backends may leave this as a no-op.
     */
    virtual void Flush() {}

    virtual const char* GetName() const = 0;
};

/******************************************************************************/
#endif // __cplusplus
#endif // ITEXT_RENDERER_H
