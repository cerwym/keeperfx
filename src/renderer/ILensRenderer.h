/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file ILensRenderer.h
 *     Abstract interface for the lens/eye-effect post-process system.
 * @par Purpose:
 *     Owns the *rendering* realization of lens effects for one backend, so the
 *     game-side lens system (LensManager / LensEffect) stays pure data and has
 *     zero rendering knowledge. Mirrors the IMapFadePass pattern:
 *
 *       - The game thread builds a pure-data IRLensCmd (effect type + params +
 *         palette + scope) in LensManager::FlushToRenderGraph().
 *       - The backend's ILensRenderer owns/caches the concrete passes
 *         (GLMistPass, VitaOverlayPass, ...) keyed by effect type, and turns a
 *         pure-data effect descriptor into a configured, ready-to-apply pass.
 *
 *     GL keeps the ping-pong FBO compositing owned by its GLLensRenderer (those
 *     are the renderer's own compositing resources), driven from EndFrame on the
 *     render thread. Software owns a CPU world-capture: BeginWorldCapture()
 *     prepares it in the draw phase, and ResolveWorldCaptureBegin/End() redirect
 *     and distort at EndFrame (the world is deferred; see the software backend).
 */
/******************************************************************************/
#pragma once

#include "bflib_video.h"   // TbGraphicsWindow

/******************************************************************************/

class ILensRenderer {
public:
    virtual ~ILensRenderer() = default;

    virtual const char* GetName() const = 0;

    /** Draw phase: prepare a world capture for this frame.
     *  Returns true when a capture is active this frame (lens ready).  GPU backends: no-op, false —
     *  they capture into an FBO on the render thread. */
    virtual bool BeginWorldCapture() { return false; }

    /** True between BeginWorldCapture() and the EndFrame resolve when a capture is
     *  active this frame.  Lets the world resolver route the deferred world through
     *  the lens without the manager tracking capture state. */
    virtual bool IsWorldCaptureActive() const { return false; }

    /** EndFrame resolve step 1 (software): return the off-screen draw target the
     *  deferred world should rasterise into (the lens buffer), given the on-screen
     *  target it would otherwise use.  The default (GPU backends / no capture)
     *  returns @p screen_target unchanged, so the world draws straight to screen. */
    virtual TbGraphicsWindow ResolveWorldCaptureBegin(const TbGraphicsWindow& screen_target)
    {
        return screen_target;
    }

    /** EndFrame resolve: distort the captured lens buffer onto @p screen_target
     *  (draw_lens_effect).  No-op if no capture is active or on GPU backends. */
    virtual void ResolveWorldCaptureEnd(const TbGraphicsWindow& /*screen_target*/) {}
};

/******************************************************************************/
