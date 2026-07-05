/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file DrawState.h
 *     Immutable per-call draw-state descriptor.
 * @par Purpose:
 *     The "how to draw" modifiers for a 2D draw — transparency, flip, outline,
 *     text alignment, and the active draw colour.  Historically this state was
 *     ambient global mutable state in `lbDisplay.DrawFlags` / `lbDisplay.DrawColour`,
 *     set by the caller before a draw and read by the rasteriser afterwards — a
 *     shared-mutable-global coupling across the engine/renderer boundary.
 *
 *     `DrawState` replaces that ambient state with an explicit, immutable value
 *     passed per draw.  It is the canonical form of the "Choice C" draw
 *     descriptor and the generalisation of two patterns already proven in-tree:
 *       - the per-command `draw_flags` on the UI IR commands (ir/UICommands.h),
 *       - `TextLayoutContext` for text (renderer/TextLayoutContext.h).
 *
 *     One representation serves both paths: the immediate renderer executes a
 *     `DrawState` now; the IR path stores it in the command and executes later.
 *
 * @par Migration note:
 *     During the lbDisplay-elimination migration, boundary shims (the `Lb*`
 *     wrappers) build a `DrawState` from the still-live `lbDisplay.DrawFlags` /
 *     `DrawColour` globals and pass it down.  Once every draw site passes a
 *     `DrawState` explicitly, those global fields are deleted (see the plan:
 *     .claude/plans — lbDisplay-elimination program, Phase 4).
 *
 *     POD, C-and-C++ compatible (usable from the legacy C bflib/engine code and
 *     the C++ renderer alike).
 */
/******************************************************************************/
#pragma once

#include "bflib_basics.h"   // TbPixel

#ifdef __cplusplus
extern "C" {
#endif

/** The per-draw "how to draw" modifiers. Immutable by convention: build one,
 *  pass it, never mutate a shared instance. */
typedef struct DrawState {
    /** Lb_SPRITE_* / Lb_TEXT_* bitmask: transparency (TRANSPAR4/8), flip
     *  (H/V), outline, and text alignment / underline / one-colour. Same bit
     *  vocabulary as the former lbDisplay.DrawFlags. */
    unsigned int flags;
    /** Active draw colour (palette index) — one-colour sprites and text.
     *  Same as the former lbDisplay.DrawColour. */
    TbPixel      colour;
} DrawState;

/** A default, no-modifiers draw state (opaque, no flip, colour 0). */
static inline DrawState draw_state_default(void)
{
    DrawState s;
    s.flags  = 0;
    s.colour = 0;
    return s;
}

/** Build a draw state from explicit flags + colour. */
static inline DrawState draw_state_make(unsigned int flags, TbPixel colour)
{
    DrawState s;
    s.flags  = flags;
    s.colour = colour;
    return s;
}

#ifdef __cplusplus
}
#endif

/******************************************************************************/
