/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IRenderer.h
 *     Abstract renderer interface.
 * @par Purpose:
 *     Defines the IRenderer interface that all renderer backends must implement.
 *     Backends provide platform-specific rendering (software, OpenGL, Vita, etc.)
 *     without the game needing to know the implementation details.
 * @par Design notes:
 *     - Pure C++ abstract base class with virtual functions.
 *     - No exceptions, no RTTI required — compatible with homebrew toolchains
 *       (devkitARM/3DS, vitasdk/Vita, devkitPPC/Wii).
 *     - SDL2 is NOT referenced here; it is an implementation detail of specific backends.
 */
/******************************************************************************/
#pragma once

#include <cstdint>
#include "BackendCapabilities.h"

/******************************************************************************/

// Forward declarations
class IWorldViewRenderer;
class IMapFadePass;
struct TbSpriteSheet;
class ITextRenderer;
class IUIRenderer;

/******************************************************************************/

/** Identifies the available renderer backends. */
enum RendererType {
    RENDERER_INVALID  = -1,
    RENDERER_AUTO     = 0,  /**< Select best available backend at startup */
    RENDERER_SOFTWARE = 1,  /**< CPU software renderer, SDL2 display output */
    RENDERER_OPENGL   = 2,  /**< OpenGL backend — framebuffer blit + GPU world geometry */
    RENDERER_VITA     = 3,  /**< PS Vita — vitaGL over GXM */
    RENDERER_3DS      = 4,  /**< Nintendo 3DS — citro3d/PICA200 */
    RENDERER_VULKAN   = 5,  /**< Vulkan backend — SDL2 window, vk-bootstrap device */
};

/******************************************************************************/

/**
 * Abstract renderer backend interface.
 *
 * A renderer backend owns the display context and is responsible for:
 *   - Initialising the display surface / GPU context
 *   - Providing a CPU-writable framebuffer (for the software rasteriser - only as a backup, you can void this.)
 *   - Presenting the completed frame to the display
 *
 * The software rasteriser always writes into the buffer returned by
 * LockFramebuffer(). What the backend does with those pixels (SDL blit,
 * GL texture upload, etc.) is an implementation detail.
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    // -------------------------------------------------------------------------
    // Lifecycle

    /** Initialise the backend and create the display context.
     *  Called once during startup (or when switching renderers at runtime).
     *  @return true on success. */
    virtual bool Init() = 0;

    /** Tear down the backend and release all display resources.
     *  Called during shutdown or before a runtime renderer switch. */
    virtual void Shutdown() = 0;

    // -------------------------------------------------------------------------
    // Per-frame interface

    /** Called at the start of each frame before any rendering takes place.
     *  @return true if the frame should proceed (false = skip this frame). */
    virtual bool BeginFrame() = 0;

    /** Called after all rendering is complete — presents the frame to the display. */
    virtual void EndFrame() = 0;

    /** Block the calling thread until any in-flight asynchronous render work is complete.
     *  Safe to call at any time; no-op when the render thread is already idle.
     *  Backends with no dedicated render thread implement this as a no-op. */
    virtual void FlushRenderWork() {}

    /** Clear the display to a palette colour index before rendering this frame.
     *  Call once per frame before any drawing takes place.
     *  GL backends store the index and resolve it against the current palette
     *  at the point glClear() runs in EndFrame().  Software backends fill the
     *  SDL draw surface immediately.  Vita/3DS backends may override as needed.
     *  @param colour_index  8-bit palette index (0 = black). */
    virtual void ClearScreen(uint8_t colour_index) { (void)colour_index; }

    // -------------------------------------------------------------------------
    // Framebuffer access (used by the software rasteriser)

    /** Returns a pointer to the writable CPU framebuffer for the current frame.
     *  The buffer is at least (*out_pitch) * ScreenHeight bytes (8-bit paletted).
     *  @param out_pitch  Receives the row pitch in bytes (may differ from width due to alignment).
     *  Must be paired with UnlockFramebuffer(). */
    virtual uint8_t* LockFramebuffer(int* out_pitch) = 0;

    /** Releases the framebuffer lock obtained via LockFramebuffer(). */
    virtual void UnlockFramebuffer() = 0;

    /** Blit a scaled RAW 8-bit paletted image to the GPU framebuffer.
     *
     *  GPU backends (OpenGL) queue the blit: the source pixels are uploaded
     *  as a GL_R8 texture and decoded via the current palette at EndFrame()
     *  time. The image is drawn as an opaque quad covering exactly the
     *  destination rectangle — index 0 is NOT transparent here.
     *
     *  Software backends return false (the caller then writes to WScreen
     *  via copy_raw8_image_buffer).
     *
     *  In OpenGL mode returning false is a fatal misconfiguration — there
     *  must be no CPU-staging fallback for GPU frontend rendering paths.
     *
     *  @param dst_width   Width of the destination rect in screen pixels.
     *  @param dst_height  Height of the destination rect in screen pixels.
     *  @param dst_x       Left edge of the destination rect in screen pixels.
     *  @param dst_y       Top edge of the destination rect in screen pixels.
     *  @param src_buf     Source 8-bit indexed image data (row-major).
     *  @param src_width   Width of the source image in pixels.
     *  @param src_height  Height of the source image in pixels.
     *  @return true if the GPU path handled the blit, false otherwise. */
    virtual bool BlitRaw8GPU(int dst_width, int dst_height, int dst_x, int dst_y,
                             const unsigned char* src_buf, int src_width, int src_height)
    {
        (void)dst_width; (void)dst_height; (void)dst_x; (void)dst_y;
        (void)src_buf; (void)src_width; (void)src_height;
        return false;
    }

    /** GPU path for a single FMV (Smacker) video frame.
     *  @param pal8_pixels        8-bit palette-indexed pixel data (row-major).
     *  @param src_w/src_h        Frame dimensions in pixels.
     *  @param src_pitch          Row stride in bytes (may differ from src_w).
     *  @param bgra_palette_1024  256 BGRA entries (4 bytes each, 1024 bytes total),
     *                            as provided by FFmpeg in AVFrame::data[1].
     *  @param dst_x/dst_y        Top-left corner of the draw rect in screen pixels,
     *                            accounting for letterboxing/pillarboxing.
     *  @param dst_w/dst_h        Draw rect dimensions in screen pixels.
     *  @return true  if the GPU path handled the frame (caller skips CPU copy).
     *          false if unhandled (software renderer — caller runs copy_to_screen). */
    virtual bool SubmitVideoFrame(
        const uint8_t* pal8_pixels, int src_w, int src_h, int src_pitch,
        const uint8_t* bgra_palette_1024,
        int dst_x, int dst_y, int dst_w, int dst_h)
    {
        (void)pal8_pixels; (void)src_w; (void)src_h; (void)src_pitch;
        (void)bgra_palette_1024;
        (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
        return false;
    }

    /** Composite a caller-owned buffer over the GPU frame with index-0 transparency.
     *
     *  Equivalent to SubmitStagingOverlay() but takes an external buffer rather
     *  than reading from the CPU staging buffer (lbDisplay.WScreen).  Use this
     *  when the caller has drawn into a local bounce buffer and must NOT write
     *  to lbDisplay.WScreen (e.g. compressed_window_draw() in GL mode).
     *
     *  The GL backend copies buf into its internal staging texture so the caller
     *  may free buf immediately after this call.
     *
     *  Software backends return false — the caller should draw directly to WScreen
     *  and call SubmitStagingOverlay().
     *
     *  @param buf  Source palette-indexed pixels (w × h, row-major).
     *  @param w/h  Buffer dimensions (must equal the physical screen dimensions).
     *  @return true  (GL: queued; transparent blit runs at EndFrame).
     *          false (software renderer or size mismatch). */
    virtual bool SubmitTransparentBlit(const uint8_t* buf, int w, int h)
    {
        (void)buf; (void)w; (void)h;
        return false;
    }

    /** Draw the creature swipe overlay in possession mode.
     *
     *  Each backend renders the swipe sprite grid (3x2, 5 frames) in its own
     *  way: the software renderer draws directly to WScreen; GPU backends
     *  render to an internal scratch buffer and upload the result as a
     *  palette-indexed texture for compositing in EndFrame().
     *
     *  @param sprites         The loaded swipe sprite sheet.
     *  @param frame           Frame index (0 – SWIPE_SPRITE_FRAMES-1).
     *  @param draw_lr         true = left-to-right, false = right-to-left (mirrored).
     *  @param engine_window_x Engine viewport X offset (for centering in LR mode). */
    virtual void DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                                  bool draw_lr, int engine_window_x)
    { (void)sprites; (void)frame; (void)draw_lr; (void)engine_window_x; }

    /** Called immediately before engine() during first-person creature view.
     *  SW: if a lens effect is active, redirects WScreen to the lens capture buffer.
     *  GPU: no-op (lens applied as post-process in EndFrame). */
    virtual void BeginLensCapture() {}

    /** Called immediately after draw_swipe_graphic() during first-person creature view.
     *  SW: applies the lens distortion from the capture buffer to WScreen and restores it.
     *  GPU: no-op. */
    virtual void EndLensCapture() {}
 
    /** GPU path for the overhead (parchment) map tile colours.
     *
     *  The caller builds a tiles_x × tiles_y byte buffer containing one
     *  palette index per map tile (one call to get_overhead_mapblock_color per
     *  tile, background=0).  The GPU uploads the buffer as a GL_R8 texture,
     *  decodes each index via the current game palette, and draws an opaque
     *  scaled quad covering dst_x/y … dst_x+dst_w/dst_y+dst_h.
     *
     *  Software backends return false — the caller must run the original
     *  block_size² pixel-per-tile WScreen write loop instead.
     *
     *  @param tile_colors  One byte per tile (tiles_x × tiles_y, row-major).
     *  @param tiles_x/y    Map tile dimensions.
     *  @param dst_x/y/w/h  Destination rectangle in screen pixels.
     *  @return true  if the GPU path accepted the data (caller skips WScreen loop).
     *          false if unhandled (software renderer). */
    virtual bool SubmitOverheadMap(
        const uint8_t* tile_colors, int tiles_x, int tiles_y,
        int dst_x, int dst_y, int dst_w, int dst_h)
    {
        (void)tile_colors; (void)tiles_x; (void)tiles_y;
        (void)dst_x; (void)dst_y; (void)dst_w; (void)dst_h;
        return false;
    }

    /** GPU path for the campaign-map zoom transition (frontzoom_to_point).
     *
     *  The source image (map_screen, 8-bit indexed, src_w×src_h) is uploaded
     *  as a GL_R8 texture and decoded via the current game palette.  A
     *  fullscreen opaque quad is drawn; each fragment's source texel is
     *  computed as:
     *    u = (center_map_x + (screen_x - screen_cx) * scale) / src_w
     *    v = (center_map_y + (screen_y - screen_cy) * scale) / src_h
     *  which exactly mirrors the arithmetic in frontzoom_to_point().
     *
     *  Software backends return false (the caller runs the CPU zoom loop).
     *
     *  @param src_buf       map_screen — 8-bit indexed pixels, row-major.
     *  @param src_w/src_h   Source image dimensions (LANDVIEW_MAP_WIDTH/HEIGHT).
     *  @param center_map_x/y  Zoom centre in map texel coordinates.
     *  @param screen_cx/cy    Zoom centre in screen pixel coordinates (y-down).
     *  @param scale           src_delta / 256.0 — source texels per screen pixel.
     *  @return true  if the GPU path accepted the frame.
     *          false if unhandled (software renderer). */
    virtual bool SubmitLandviewZoom(
        const uint8_t* src_buf, int src_w, int src_h,
        float center_map_x, float center_map_y,
        float screen_cx,    float screen_cy,
        float scale)
    {
        (void)src_buf; (void)src_w; (void)src_h;
        (void)center_map_x; (void)center_map_y;
        (void)screen_cx;    (void)screen_cy;
        (void)scale;
        return false;
    }

    // -------------------------------------------------------------------------
    // Metadata

    /** Human-readable name for this backend (e.g. "Software", "OpenGL"). */
    virtual const char* GetName() const = 0;

    /** Returns true if this backend supports switching to/from it at runtime
     *  without requiring a full application restart. */
    virtual bool SupportsRuntimeSwitch() const = 0;

    /** Returns a struct of capability flags for this backend.
     *  Callers should use RendererGetCapabilities() (RendererManager.h) rather
     *  than RendererGetActiveType() to avoid hard-coding backend identities.
     *  The result is a trivially-copyable POD — callers may store it by value. */
    virtual BackendCapabilities GetCapabilities() const = 0;

    /** Schedule a screenshot to be saved to @p path.
     *  The backend captures the current (or next) rendered frame and saves it.
     *  Returns true if the request was accepted (success is logged by the backend);
     *  returns false if screenshots are not supported by this backend. */
    virtual bool ScheduleScreenshot(const char* /*path*/, int /*fmt*/) { return false; }

    // -------------------------------------------------------------------------
    // Sub-renderer access (managed internally by each backend)

    /** Returns the world view renderer managed by this backend.
     *  Each renderer type creates the appropriate implementation internally. */
    virtual class IWorldViewRenderer* GetWorldViewRenderer() = 0;

    /** Returns the map fade pass managed by this backend.
     *  Each renderer type creates the appropriate implementation internally. */
    virtual class IMapFadePass* GetMapFadePass() = 0;

    /** Returns the text renderer managed by this backend.
     *  Each renderer type creates the appropriate implementation internally. */
    virtual class ITextRenderer* GetTextRenderer() = 0;

    /** Returns the UI renderer managed by this backend.
     *  Each renderer type creates the appropriate implementation internally. */
    virtual class IUIRenderer* GetUIRenderer() = 0;

    // -------------------------------------------------------------------------
    // Lifecycle notifications from the game thread

    /** Called when tile/block textures are reloaded (e.g. resolution change).
     *  GPU backends should invalidate their tile atlas here.
     *  Default: no-op. */
    virtual void NotifyTexturesReloaded() {}

    /** Called after game tables (render_fade_tables etc.) are ready.
     *  GPU backends schedule fade-table texture creation on the render thread.
     *  Default: no-op. */
    virtual void NotifyGameTablesReady() {}

    // -------------------------------------------------------------------------
    // Optional GPU-only submission methods (default: no-op)

    /** Submit tile data for the zoom-box overlay (GPU path).
     *  Software backends ignore this call. */
    virtual void SubmitZoomBoxTiles(const uint16_t* /*tile_block_ids*/,
                                    int /*tiles_x*/, int /*tiles_y*/,
                                    int /*dst_x*/, int /*dst_y*/,
                                    int /*tile_w*/, int /*tile_h*/) {}

    /** Schedule a Picture-in-Picture isometric render into an FBO (GPU path).
     *  Software backends ignore this call. */
    virtual void SubmitPiPRender(struct Camera* /*cam*/,
                                 int /*x*/, int /*y*/,
                                 int /*w*/, int /*h*/) {}
};

/******************************************************************************/
