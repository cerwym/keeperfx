/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IWorldViewRenderer.h
 *     Abstract interface for 3D world-view rendering.
 * @par Purpose:
 *     Separates the 3D world-view rendering pipeline (bucket submission + rasterizer)
 *     from the rest of the game. The bucket-fill step (geometry walk, column
 *     traversal, polygon-add calls) is platform-neutral and stays unchanged.
 *     Only the draw step — what is done with the accumulated bucket list —
 *     is abstracted here.
 *
 * @par Design:
 *     Three calls per frame for each rendered view:
 *       1. BeginWorldPass()  — bind the target framebuffer and setup rasterizer
 *       2. [game code fills the bucket list via draw_view() / draw_frontview_engine()]
 *       3. DrawIsometricView() or DrawFrontView() — rasterize the bucket list
 *
 *     Software implementation:
 *       BeginWorldPass     → setup_vecs(framebuf, NULL, pitch, w, h)
 *       DrawIsometricView  → display_drawlist()
 *       DrawFrontView      → display_fast_drawlist(cam)
 *
 *     Future GPU implementation:
 *       BeginWorldPass     → begin GPU geometry pass
 *       DrawIsometricView  → submit bucket list as GPU draw calls
 *       DrawFrontView      → submit front-view bucket list as GPU draw calls
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
     *  @param h         Viewport height in pixels.
     *  @param vp_x      Viewport left edge in screen pixels (0 if no sidebar).
     *  @param vp_y      Viewport top edge in screen pixels (0 if no sidebar). */
    virtual void BeginWorldPass(uint8_t* framebuf, int pitch, int w, int h,
                                int vp_x, int vp_y) = 0;

    // =========================================================================
    // Draw

    /** Rasterize the isometric/1st-person bucket list into the bound framebuffer.
     *  Wraps display_drawlist() on the software path. */
    virtual void DrawIsometricView() = 0;

    /** Rasterize the front-view bucket list into the bound framebuffer.
     *  Wraps display_fast_drawlist(cam) on the software path.
     *  @param cam  Camera used for front-view projection. */
    virtual void DrawFrontView(struct Camera* cam) = 0;

    // =========================================================================
    // Info

    /** Human-readable backend name (e.g. "SOFTWARE", "VITA_GPU"). */
    virtual const char* GetName() const = 0;

    // =========================================================================
    // Keeper sprite (world-space billboarded sprites)

    /** Submit a keeper-sprite (creature/object/shadow) for GPU rendering.
     *  dst_x/y/w/h = screen destination rect (pixels).
     *  data        = raw RLE palette-index sprite data.
     *  src_w/h     = sprite source dimensions.
     *  draw_flags  = lbDisplay.DrawFlags at time of call (flip, transpar, remap, additive).
     *  remap       = colour remap table (may be NULL).
     *  Returns 1 if the GPU handled it (CPU blit should be skipped), 0 to fall back. */
    virtual int SubmitKeeperSprite(long dst_x, long dst_y, long dst_w, long dst_h,
                                   const unsigned char* data, int src_w, int src_h,
                                   unsigned int draw_flags, const unsigned char* remap)
    {
        (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
        (void)data;  (void)src_w; (void)src_h;
        (void)draw_flags; (void)remap;
        return 0;
    }
};

/******************************************************************************/
#endif // IWORLD_VIEW_RENDERER_H
