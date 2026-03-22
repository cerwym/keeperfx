/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IWorldViewRenderer.h
 *     Abstract interface for 3D world-view rendering.
 * @par Purpose:
 *     Separates the 3D world-view rendering pipeline (bucket flush + rasterizer)
 *     from the rest of the game. The bucket-fill phase (geometry walk, column
 *     traversal, polygon-add calls) is platform-neutral and stays unchanged.
 *     Only the flush phase — what is done with the accumulated bucket list —
 *     is abstracted here.
 *
 * @par Design:
 *     Three calls per frame for each rendered view:
 *       1. BeginWorldPass()  — bind the target framebuffer and setup rasterizer
 *       2. [game code fills the bucket list via draw_view() / draw_frontview_engine()]
 *       3. FlushIsometricView() or FlushFrontView() — rasterize the bucket list
 *
 *     Software implementation:
 *       BeginWorldPass  → setup_vecs(framebuf, NULL, pitch, w, h)
 *       FlushIsometric  → display_drawlist()
 *       FlushFrontView  → display_fast_drawlist(cam)
 *
 *     Future GPU implementation:
 *       BeginWorldPass  → begin GPU geometry pass
 *       FlushIsometric  → submit bucket list as GPU draw calls
 *       FlushFrontView  → submit front-view bucket list as GPU draw calls
 */
/******************************************************************************/
#ifndef IWORLD_VIEW_RENDERER_H
#define IWORLD_VIEW_RENDERER_H

#include <cstdint>

// Forward-declare Camera (defined in globals.h / game_legacy.h)
struct Camera;

/******************************************************************************/

class IWorldViewRenderer {
public:
    virtual ~IWorldViewRenderer() = default;

    // =========================================================================
    // Frame setup

    /** Bind the target CPU framebuffer and configure the rasterizer.
     *  Must be called before any bucket-add calls for the current view.
     *  @param framebuf  Pointer to the first scanline of the framebuffer
     *                   (same pointer returned by RendererLockFramebuffer).
     *  @param pitch     Row stride in bytes (may be wider than width).
     *  @param w         Viewport width in pixels.
     *  @param h         Viewport height in pixels. */
    virtual void BeginWorldPass(uint8_t* framebuf, int pitch, int w, int h) = 0;

    // =========================================================================
    // Flush

    /** Rasterize the isometric/1st-person bucket list into the bound framebuffer.
     *  Wraps display_drawlist() on the software path. */
    virtual void FlushIsometricView() = 0;

    /** Rasterize the front-view bucket list into the bound framebuffer.
     *  Wraps display_fast_drawlist(cam) on the software path.
     *  @param cam  Camera used for front-view projection. */
    virtual void FlushFrontView(struct Camera* cam) = 0;

    // =========================================================================
    // Info

    /** Human-readable backend name (e.g. "SOFTWARE", "VITA_GPU"). */
    virtual const char* GetName() const = 0;
};

/******************************************************************************/
#endif // IWORLD_VIEW_RENDERER_H
