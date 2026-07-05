/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file DrawState.h
 *     Immutable per-call draw-state descriptor.
 * @par Purpose:
 *     The "how to draw" modifiers for a 2D draw — transparency, flip, outline,
 *     text alignment, and the active draw colour.  
 *
 * @par Naming:
 *     Named `KfxDrawState` (not `DrawState`) because `DrawState` collides with the
 *     Win32 GDI `DrawState`/`DrawStateA` macro from <windows.h>.
 *
 * @par Migration note:
 *     During the lbDisplay-elimination migration, boundary shims (the `Lb*`
 *     wrappers / `UIRenderer_*` bridges) build a `KfxDrawState` from the still-live
 *     `lbDisplay.DrawFlags` / `DrawColour` globals and pass it down.  Once every
 *     draw site passes a `KfxDrawState` explicitly, those global fields are deleted
 *
 *     POD, C-and-C++ compatible (usable from the legacy C bflib/engine code and
 *     the C++ renderer alike).
 */
/******************************************************************************/
#pragma once

// Dependency-free (only builtin types) so it can be included very early, before
// bflib type headers.  `colour` is a palette index == TbPixel == unsigned char.

#ifdef __cplusplus
extern "C" {
#endif

/** The per-draw "how to draw" modifiers. Immutable by convention: build one,
 *  pass it, never mutate a shared instance. */
typedef struct KfxDrawState {
    /** Lb_SPRITE_* / Lb_TEXT_* bitmask: transparency (TRANSPAR4/8), flip
     *  (H/V), outline, and text alignment / underline / one-colour. Same bit
     *  vocabulary as the former lbDisplay.DrawFlags. */
    unsigned int flags;
    /** Active draw colour (palette index) — one-colour sprites and text.
     *  Same as the former lbDisplay.DrawColour (TbPixel == unsigned char). */
    unsigned char colour;
} KfxDrawState;

/** A default, no-modifiers draw state (opaque, no flip, colour 0). */
static inline KfxDrawState draw_state_default(void)
{
    KfxDrawState s;
    s.flags  = 0;
    s.colour = 0;
    return s;
}

/** Build a draw state from explicit flags + colour. */
static inline KfxDrawState draw_state_make(unsigned int flags, unsigned char colour)
{
    KfxDrawState s;
    s.flags  = flags;
    s.colour = colour;
    return s;
}

#ifdef __cplusplus
}
#endif

/******************************************************************************/
