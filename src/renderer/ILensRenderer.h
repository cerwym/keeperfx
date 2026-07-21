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
 *     render thread. Software owns the immediate-mode world-capture bracket
 *     (BeginWorldCapture/EndWorldCapture) and its CPU distortion path.
 */
/******************************************************************************/
#pragma once

/******************************************************************************/

class ILensRenderer {
public:
    virtual ~ILensRenderer() = default;

    virtual const char* GetName() const = 0;

    /** Begin capturing the world view for lens distortion.
     *
     *  This is the immediate-mode "world-capture bracket" the software backend
     *  needs: it runs on the game thread, mid-frame, around engine()+swipe in
     *  draw_creature_view(). The software backend redirects WScreen to an
     *  offscreen lens buffer here so only the world (UI excluded) is captured.
     *
     *  GPU backends leave this a no-op: they capture the world into an FBO from
     *  the render thread via the pure-data IR (BeginSceneCapture in EndFrame),
     *  so there is nothing to do on the game thread. */
    virtual void BeginWorldCapture() {}

    /** End the world-capture bracket and apply the lens distortion.
     *
     *  Software: restores WScreen and distorts the captured buffer back onto the
     *  screen (draw_lens_effect). GPU backends: no-op (applied on the render
     *  thread in EndFrame). Paired with BeginWorldCapture(); safe to call when no
     *  capture is active. */
    virtual void EndWorldCapture() {}
};

/******************************************************************************/
