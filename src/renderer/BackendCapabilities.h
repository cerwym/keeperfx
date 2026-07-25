/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file BackendCapabilities.h
 *     Backend capability flags for the renderer abstraction layer.
 * @par Purpose:
 *     Describes what a renderer backend supports.  Callers query these flags
 *     instead of checking RendererGetActiveType() so that adding new backends
 *     only requires updating the backend's GetCapabilities() implementation.
 * @par Design notes:
 *     - Plain struct of bools — trivially copyable, no vtable.
 *     - C99/C++11 compatible (uses bool / stdbool.h).
 *     - Cached in RendererManager; refreshed on every backend switch.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Flags describing what a renderer backend can do. */
struct BackendCapabilities {

    /** True when the backend renders the 3D world via GPU geometry rather than
     *  the CPU software rasteriser.  Implies wantsFullscreenViewport. */
    int hasGPURenderPath;

    /** True when the backend wants the engine viewport to span the full screen
     *  width (with the UI drawn on top), rather than being offset by
     *  status_panel_width. */
    int wantsFullscreenViewport;

    /** True when the backend composites the minimap over the panel artwork
     *  itself, so submitted minimap pixels must NOT have the panel-background
     *  blend baked into them.*/
    int compositesMinimapBackground;

    /** True when the backend can capture movie frames into a CPU buffer.
     *  Only the software renderer supports this today.  scrcapt.c uses it. */
    int supportsMovieCapture;
};

#ifdef __cplusplus
} // extern "C"
#endif

/******************************************************************************/
