/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file TextCommands.h
 *     Intermediate-representation command types for the text renderer.
 * @par Purpose:
 *     Captures text draw calls made on the game thread (DrawTextResized,
 *     DrawTextAt) as self-contained commands the render thread can execute
 *     without reading lbDisplay globals or holding the game-thread text state.
 *
 *     All window/clip/layout state is snapshotted at submission time;
 *     the text string is copied into a fixed-length inline buffer.  Very long
 *     strings are truncated at kIRTextMaxLen - 1 bytes (null-terminated).
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "renderer/ir/IRCommandBuffer.h"

/******************************************************************************/

static constexpr size_t kIRTextMaxLen = 256;

/** Draw a text string, either with window/wrap (Resized) or at an absolute
 *  screen position (At).  Both variants are unified here. */
struct IRTextDrawCmd
{
    int32_t  pos_x          = 0;     /**< Anchor X (window-relative for Resized, absolute for At). */
    int32_t  pos_y          = 0;
    int32_t  units_per_px   = 16;    /**< 16 = 100% scale. */
    uint8_t  absolute       = 0;     /**< 1 = DrawTextAt (no window/wrap). */
    uint8_t  draw_colour    = 0;     /**< Snapshot of lbDisplay.DrawColour. */
    uint16_t draw_flags     = 0;     /**< Snapshot of lbDisplay.DrawFlags. */

    /* Window / clipping state snapshot (for Resized path). */
    int32_t  justify_x      = 0;
    int32_t  justify_y      = 0;
    int32_t  justify_w      = 0;
    int32_t  clip_x         = 0;
    int32_t  clip_y         = 0;
    int32_t  clip_w         = 0;
    int32_t  clip_h         = 0;

    /* Font references (non-owning). Guarded by font_generation to reject
     * stale pointers after sprite/font reload transitions. */
    const void* font        = nullptr;     /**< Pointer to TbSpriteSheet; opaque here. */
    const void* dbc_font    = nullptr;     /**< Pointer to AsianFont; null if N/A. */
    uint8_t     dbc_enabled = 0;
    uint32_t    font_generation = 0;       /**< Snapshot from RendererGetTextFontGeneration(). */
    long        dbc_colour0 = 0;           /**< DBC face colour (from LbTextGetFontFaceColor). */
    long        dbc_colour1 = 0;           /**< DBC shadow colour (currently unused). */

    /* Inline text storage — avoids heap allocation per command. */
    char text[kIRTextMaxLen] = {};

    /** Helper: set text from a C string, truncating at kIRTextMaxLen-1. */
    void SetText(const char* src)
    {
        if (src)
        {
            std::strncpy(text, src, kIRTextMaxLen - 1);
            text[kIRTextMaxLen - 1] = '\0';
        }
        else
        {
            text[0] = '\0';
        }
    }
};

/******************************************************************************/

/** Combined per-frame text command buffers. */
struct TextCommandBuffers
{
    IRCommandBuffer<IRTextDrawCmd> draws;

    void Reset()   { draws.Reset(); }
    void Reserve(size_t n) { draws.Reserve(n); }
    void Swap(TextCommandBuffers& other) { draws.Swap(other.draws); }
};

/******************************************************************************/
