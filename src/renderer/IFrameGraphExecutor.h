/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IFrameGraphExecutor.h
 *     Backend hook interface driven by RenderGraph::Execute().
 */
/******************************************************************************/
#pragma once

/******************************************************************************/

/**
 * Ordered per-frame phase hooks realised by a rendering backend.
 *
 * The call order is owned by RenderGraph::Execute(); see that function for the
 * canonical sequence and the constraints (world inside the lens FBO bracket,
 * map-fade after world but before image-presents/overhead, etc.).
 */
class IFrameGraphExecutor
{
public:
    virtual ~IFrameGraphExecutor() = default;

    // ── Frame setup ────────────────────────────────────────────────────────
    /** Clear the default framebuffer to the frame's clear colour. */
    virtual void FGClearFrame() = 0;
    /** Populate render-thread UI draw buffers from the UI IR snapshot. */
    virtual void FGPopulateUI() = 0;

    // ── World inside the lens scene-capture bracket ────────────────────────
    /** Begin lens scene capture (redirect world to the lens FBO) if a lens is
     *  active this frame; records bracket state for FGResolveWorldCapture(). */
    virtual void FGBeginWorldCapture() = 0;
    /** Execute world geometry from the world IR (into the lens FBO if bracketed). */
    virtual void FGExecuteWorld() = 0;
    /** Draw swipe-overlay quads while the lens FBO is still bound. */
    virtual void FGFlushSwipeOverlay() = 0;
    /** Resolve + composite the lens FBO back to the default framebuffer (no-op
     *  when the bracket was not opened). */
    virtual void FGResolveWorldCapture() = 0;
    /** Re-upload the base palette for WorldOnly lens scope so subsequent UI/text
     *  decode without the lens tint (no-op otherwise). */
    virtual void FGApplyLensPaletteUIExclusion() = 0;

    // ── Map-fade compose (after world, before presents/overhead) ───────────
    /** GPU map-fade compose pass (parchment transitions); no-op when inactive. */
    virtual void FGExecuteMapFade() = 0;

    // ── World-space sprite / flat overlay layers ───────────────────────────
    virtual void FGDrawWorldSpriteLayer() = 0;
    virtual void FGDrawWorldOverlayFlatLayer() = 0;

    // ── Full-screen image presents (backgrounds / parchment / FMV) ─────────
    /** Composite queued image presents (skipped when already captured into the
     *  map-fade FBO this frame). */
    virtual void FGExecuteImagePresents() = 0;

    // ── Overhead map / PiP / zoom box ──────────────────────────────────────
    virtual void FGDrawOverheadMap() = 0;
    virtual void FGExecutePiPCaptures() = 0;

    // ── Game UI, then deferred world-view capture ──────────────────────────
    virtual void FGDrawGameUI() = 0;
    /** Take the deferred map-fade world-view capture if one was flagged (after
     *  GameUI so the sidebar is part of the snapshot); no-op otherwise. */
    virtual void FGCaptureWorldFrameIfPending() = 0;

    // ── Zoom-box tiles (on top of GameUI), front overlay, text ─────────────
    virtual void FGDrawZoomBoxes() = 0;
    virtual void FGDrawFrontOverlay() = 0;
    virtual void FGExecuteText() = 0;

    // ── Full-screen tint, cursor, dev overlay ──────────────────────────────
    virtual void FGDrawScreenTint() = 0;
    virtual void FGExecuteCursor() = 0;
    virtual void FGDrawDevToolsOverlay() = 0;

    // ── Present-time captures ──────────────────────────────────────────────
    /** Read back the composited backbuffer to a screenshot file if one is queued. */
    virtual void FGCaptureScreenshot() = 0;
    /** scRGB gamma-lift redraw for HDR / extended-range swapchains; no-op otherwise. */
    virtual void FGApplyScrgbLift() = 0;
};

/******************************************************************************/
