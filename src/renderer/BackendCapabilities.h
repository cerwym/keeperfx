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

    /** True when SubmitOverheadMap() is handled in hardware (GPU backends).
     *  frontmenu_ingame_map.c uses this to skip the WScreen tile-loop. */
    int hasGPUOverheadMap;

    /** True when IMapFadePass manages its own frame capture internally.
     *  prepare_map_fade_buffers() should be skipped when this is set. */
    int hasGPUMapFade;

    /** True when SubmitTransparentBlit() / SubmitStagingOverlay() are handled
     *  in hardware.  gui_parchment.c uses this to choose the overlay path. */
    int hasGPUOverlay;

    /** True when SubmitLandviewZoom() is handled in hardware.
     *  front_landview.c uses this to skip the CPU zoom loop. */
    int hasGPULandviewZoom;

    /** True when SubmitVideoFrame() is handled in hardware.
     *  bflib_fmvids.cpp uses this to skip the CPU copy-to-screen loop. */
    int hasGPUVideoFrame;

    /** True when the GPU non-spatial-sprite bucket walker should run.
     *  engine_render.c:draw_frontview_engine() uses this. */
    int hasGPUSprites;

    /** True when DrawSwipeOverlay() has a GPU implementation that composites
     *  the possession attack animation via the render thread. */
    int hasSwipeOverlay;

    /** True when the backend can be switched to/from at runtime without
     *  requiring a full application restart. */
    int supportsRuntimeSwitch;

    /** True when the backend supports GPU post-process lens passes via
     *  IPostProcessPass.  LensManager uses this to skip CPU Draw() calls. */
    int supportsGPUPasses;

    /** Reserved for the future Shadow renderer. */
    int supportsShadowMap;

    /** Reserved for the future Debug overlay renderer. */
    int supportsDebugOverlay;

    /** True when the backend can capture movie frames into a CPU buffer.
     *  Only the software renderer supports this today.  scrcapt.c uses it. */
    int supportsMovieCapture;

    /** True when the backend implements ScheduleScreenshot().
     *  When false, take_screenshot() returns false and the UI shows "Cannot save". */
    int supportsScreenshot;
};

#ifdef __cplusplus
} // extern "C"
#endif

/******************************************************************************/
