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
#ifndef IRENDERER_H
#define IRENDERER_H

#include <cstdint>

/******************************************************************************/

/** Identifies the available renderer backends. */
enum RendererType {
    RENDERER_INVALID  = -1,
    RENDERER_AUTO     = 0,  /**< Select best available backend at startup */
    RENDERER_SOFTWARE = 1,  /**< CPU software renderer, SDL2 display output */
    RENDERER_OPENGL   = 2,  /**< OpenGL backend — framebuffer blit (Phase 1) */
    RENDERER_VITA     = 3,  /**< PS Vita — vitaGL over GXM */
    RENDERER_3DS      = 4,  /**< Nintendo 3DS — citro3d/PICA200 */
};

/******************************************************************************/

/**
 * Abstract renderer backend interface.
 *
 * A renderer backend owns the display context and is responsible for:
 *   - Initialising the display surface / GPU context
 *   - Providing a CPU-writable framebuffer (for the software rasteriser)
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

    // -------------------------------------------------------------------------
    // Framebuffer access (used by the software rasteriser)

    /** Returns a pointer to the writable CPU framebuffer for the current frame.
     *  The buffer is at least (*out_pitch) * ScreenHeight bytes (8-bit paletted).
     *  @param out_pitch  Receives the row pitch in bytes (may differ from width due to alignment).
     *  Must be paired with UnlockFramebuffer(). */
    virtual uint8_t* LockFramebuffer(int* out_pitch) = 0;

    /** Releases the framebuffer lock obtained via LockFramebuffer(). */
    virtual void UnlockFramebuffer() = 0;

    // -------------------------------------------------------------------------
    // Metadata

    /** Human-readable name for this backend (e.g. "Software", "OpenGL"). */
    virtual const char* GetName() const = 0;

    /** Returns true if this backend supports switching to/from it at runtime
     *  without requiring a full application restart. */
    virtual bool SupportsRuntimeSwitch() const = 0;

    /** Returns true if this backend can execute IPostProcessPass GPU lens effects.
     *  When true, RendererVita runs GPU passes in EndFrame() and LensManager
     *  skips the corresponding CPU Draw() calls. */
    virtual bool SupportsGPUPasses() const { return false; }
};

/******************************************************************************/
#endif // IRENDERER_H
