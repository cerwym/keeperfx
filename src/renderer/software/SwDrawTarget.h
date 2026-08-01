/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SwDrawTarget.h
 *     Renderer-private "current software draw target" for the legacy
 *     bflib_vidraw.* rasteriser primitives.
 * @par Purpose:
 *     The low-level software primitives (lines, boxes, filled rects, sprite
 *     blits) are called from hundreds of engine sites that never hold a draw
 *     target.  They historically read the ambient target through the public
 *     RendererGetWScreen()/RendererScreenWidth()/RendererGraphicsWindow*()
 *     accessors.  This module gives them a renderer-private target instead:
 *
 *       - The clip WINDOW always tracks the active renderer viewport.
 *       - The SURFACE (base pointer + pitch + height) defaults to the renderer's
 *         framebuffer but can be overridden by an explicit scope guard
 *         (SwTargetScope / SwTargetPushSurface) — e.g. the possession lens
 *         redirecting the deferred world into an off-screen buffer.
 *
 *     The override is the ONLY way to point the primitives at a different
 *     surface; there is no more ad-hoc RendererSetWScreen() mutation for
 *     redirects.  This is the pragmatic terminal state for the legacy primitives
 *     (Phase C2): ambient reads survive only here, renderer-private and scoped.
 */
/******************************************************************************/
#pragma once

#include "bflib_video.h"   /* TbPixel */

#ifdef __cplusplus
extern "C" {
#endif

/** Surface base of the current software target (former RendererGetWScreen). */
TbPixel* SwTargetWScreen(void);

/** Surface base advanced to the current clip-window origin
 *  (former RendererGetGraphicsWindowPtr). */
TbPixel* SwTargetGraphicsWindowPtr(void);

/** Surface pitch in bytes per row (former RendererScreenWidth). */
long SwTargetScanline(void);

/** Full surface height in rows (former RendererScreenHeight). */
long SwTargetScreenHeight(void);

/** Current clip/graphics window rect (former RendererGraphicsWindow*). */
long SwTargetWindowX(void);
long SwTargetWindowY(void);
long SwTargetWindowWidth(void);
long SwTargetWindowHeight(void);

/** Override the target surface (base/pitch/height) until SwTargetPopSurface().
 *  The clip window is left untouched.  One level of save/restore is kept, so a
 *  single nested push is safe (the redirect sites do not nest in practice). */
void SwTargetPushSurface(TbPixel* base, long pitch, long height);
void SwTargetPopSurface(void);

#ifdef __cplusplus
} // extern "C"

/** RAII surface-override scope for C++ callers (the possession-lens world pass).
 *  Points the legacy primitives at @p base for the scope's lifetime. */
struct SwTargetScope {
    SwTargetScope(TbPixel* base, long pitch, long height)
    { SwTargetPushSurface(base, pitch, height); }
    ~SwTargetScope() { SwTargetPopSurface(); }
    SwTargetScope(const SwTargetScope&) = delete;
    SwTargetScope& operator=(const SwTargetScope&) = delete;
};
#endif
