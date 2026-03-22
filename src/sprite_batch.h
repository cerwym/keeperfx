/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file sprite_batch.h
 *     UI sprite batch dispatch interface.
 * @par Purpose:
 *     Provides a C function-pointer vtable that intercepts LbSpriteDraw() and
 *     LbSpriteDrawOneColour() calls and can redirect them to a hardware GPU
 *     batch pass instead of the CPU software blitter.
 *
 *     When g_sprite_batch is NULL (the default), all calls fall through to the
 *     existing software path — zero overhead on platforms that do not
 *     implement a hardware sprite layer.
 *
 *     The design mirrors AudioInterface: plain C, no GL types, no C++ in the
 *     header — compatible with all target toolchains (vitasdk, devkitARM/3DS,
 *     MinGW, MSVC).
 *
 * @par Lifecycle contract for implementors:
 *     1. on_sheet_loaded() is called from load_spritesheet() after a new
 *        TbSpriteSheet is fully populated.  The pointer is stable for the
 *        lifetime of the sheet.  The implementation should upload sprite data
 *        to the GPU atlas here (or lazily on first draw).
 *     2. on_sheet_freed() is called from free_spritesheet() before the
 *        TbSpriteSheet is deleted.  The pointer must not be dereferenced after
 *        this call returns.
 *     3. begin_frame() is called once per frame before any draw calls.
 *     4. submit_sprite() / submit_sprite_one_colour() accumulate draw commands.
 *        They receive virtual game coordinates (origin is the current
 *        lbDisplay.GraphicsWindowX/Y), and draw_flags mirrors lbDisplay.DrawFlags
 *        at the moment of the call.
 *     5. flush() is called once per frame after all draw calls are done.
 *        The implementation issues the actual GPU draw and clears the list.
 */
/******************************************************************************/
#ifndef SPRITE_BATCH_H
#define SPRITE_BATCH_H

#include "bflib_basics.h"   /* TbResult, TbPixel */
#include "bflib_sprite.h"   /* TbSprite, TbSpriteSheet */

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************/
/* draw_flags bits forwarded from lbDisplay.DrawFlags                         */
/******************************************************************************/

/* These match the Lb_SPRITE_* flags defined in bflib_video.h.               */
/* Redefined here so sprite_batch.h is self-contained.                        */
#ifndef SPRITE_BATCH_FLAGS_DEFINED
#define SPRITE_BATCH_FLAGS_DEFINED
#define SB_FLAG_TRANSPAR4    0x0004   /**< 50% alpha blend (colour is source) */
#define SB_FLAG_TRANSPAR8    0x0008   /**< 50% alpha blend (colour is dest)   */
#define SB_FLAG_FLIP_HORIZ   0x0001   /**< Mirror sprite left-right           */
#define SB_FLAG_FLIP_VERTIC  0x0002   /**< Mirror sprite top-bottom           */
#endif

/******************************************************************************/
/* SpriteBatchInterface                                                        */
/******************************************************************************/

/** Function-pointer table for the hardware UI sprite batch.
 *  All pointers must be non-NULL when g_sprite_batch is non-NULL. */
typedef struct SpriteBatchInterface {
    /** Called after load_spritesheet() fully populates a new sheet.
     *  @param sheet     Pointer to the new TbSpriteSheet (stable until freed).
     *  @param sheet_id  Opaque integer id assigned by the implementation;
     *                   assigned IN the callback by incrementing an internal
     *                   counter — pass 0, implementation fills it in via the
     *                   TbSpriteSheet pointer used as the key. */
    void (*on_sheet_loaded)(const struct TbSpriteSheet* sheet);

    /** Called before free_spritesheet() deletes a TbSpriteSheet. */
    void (*on_sheet_freed)(const struct TbSpriteSheet* sheet);

    /** Accumulate one sprite draw command.
     *  @param x          X position in virtual game coordinates.
     *  @param y          Y position in virtual game coordinates.
     *  @param spr        Sprite to draw.
     *  @param draw_flags lbDisplay.DrawFlags at call time (SB_FLAG_* subset). */
    TbResult (*submit_sprite)(long x, long y,
                              const struct TbSprite* spr,
                              unsigned int draw_flags);

    /** Accumulate a one-colour sprite draw command.
     *  @param colour  Palette index used as the fill colour (0-255). */
    TbResult (*submit_sprite_one_colour)(long x, long y,
                                        const struct TbSprite* spr,
                                        unsigned char colour,
                                        unsigned int draw_flags);

    /** Called once per frame before any submit_* calls. */
    void (*begin_frame)(void);

    /** Called once per frame after all submit_* calls — issues GPU draw. */
    void (*flush)(void);

    /** Called from LbPaletteSet() after lbPalette has been updated (6-bit, 768
     *  bytes).  The implementation should re-decode any previously added
     *  sheets using the new palette — atlas pixels decoded at sheet-load time
     *  may have used a zero/stale palette and need refreshing. */
    void (*on_palette_set)(const unsigned char* lbPalette);

} SpriteBatchInterface;

/******************************************************************************/
/* Global instance — NULL means software fallback (default on all platforms). */
/******************************************************************************/

extern SpriteBatchInterface* g_sprite_batch;

#ifdef __cplusplus
}
#endif

#endif /* SPRITE_BATCH_H */
