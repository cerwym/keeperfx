/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IMapFadePass.h
 *     Abstract interface for the map-fade transition effect.
 * @par Purpose:
 *     Abstracts the parchment ↔ 3D-view transition wipe effect.
 *
 *     The transition runs in two directions:
 *       - FadeIn:  step 0 → 32  (parchment collapses to reveal 3D view)
 *       - FadeOut: step 32 → 0  (3D view dissolves into parchment)
 *     Each step advances by 4, so 9 frames total.
 *
 *     Each call to StepFadeIn() / StepFadeOut() renders one frame and returns
 *     the next step value.  The caller stores the returned value and passes it
 *     back on the next iteration.
 *
 * @par Software implementation (SoftwareMapFadePass):
 *       StepFadeIn(step)  → engine_redraw_map_fade_in(step)   → map_fade_in(step)
 *       StepFadeOut(step) → engine_redraw_map_fade_out(step)  → map_fade_out(step)
 *
 * @par GPU implementation (GLMapFadePass):
 *       StepFadeIn/Out: on first call captures both views via prepare_map_fade_buffers(),
 *       decodes palette to RGBA, uploads two GL_RGBA8 textures.  Sets a pending-step
 *       flag so EndFrame() can call RenderGPUComposePass() to composite the wipe at
 *       native resolution using MAP_FADE_FRAG_SHADER (UV-warp + additive RGB blend).
 */
/******************************************************************************/
#ifndef IMAP_FADE_PASS_H
#define IMAP_FADE_PASS_H

#include <stdint.h>

/******************************************************************************/

class IMapFadePass {
public:
    virtual ~IMapFadePass() = default;

    /** Render one fade-in frame (parchment → 3D view).
     *  On step == 0, captures the source and destination frames.
     *  @param step  Current step value in [0, 32].
     *  @return      Next step value (step + 4, capped at 32). */
    virtual int32_t StepFadeIn(int32_t step) = 0;

    /** Render one fade-out frame (3D view → parchment).
     *  On step == 32, captures the source and destination frames.
     *  @param step  Current step value in [0, 32].
     *  @return      Next step value (step - 4, capped at 0). */
    virtual int32_t StepFadeOut(int32_t step) = 0;

    virtual const char* GetName() const = 0;

    /** Returns true when this pass wants to render a fullscreen wipe quad at
     *  EndFrame() time (after the staging palette blit).  The software
     *  implementation always returns false — it writes directly to WScreen.
     *  GLMapFadePass returns true after CaptureAndUploadFrames() succeeds. */
    virtual bool HasGPUComposePass() const { return false; }

    /** Called by RendererOpenGL::EndFrame() when HasGPUComposePass() is true.
     *  Renders the wipe quad using the pending step value set during the most
     *  recent StepFadeIn / StepFadeOut call.  No-op by default. */
    virtual void RenderGPUComposePass() {}
};

/******************************************************************************/
#endif // IMAP_FADE_PASS_H
