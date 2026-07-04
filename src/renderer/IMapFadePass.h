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
#pragma once

#include <stdint.h>

// Forward declarations — avoid pulling in RenderGraph and post-process IR
// headers into software/vita backends that don't use them.
class RenderGraph;
struct IRMapFadeCmd;

/******************************************************************************/

class IMapFadePass {
public:
    virtual ~IMapFadePass() = default;

    /** Capture the source and destination frames for the fade transition.
     *  Software fills the provided CPU buffers; GPU backends usually ignore this. */
    virtual void PrepareBuffers(uint8_t* fade_src, uint8_t* fade_dest, int scanline, int height) = 0;

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

    /** Notify the pass of the current OS-window dimensions.
     *  GPU backends update render target sizes here.
     *  Default: no-op. */
    virtual void SetScreenSize(int /*w*/, int /*h*/) {}

    /** Called by RendererOpenGL::EndFrame() (game thread) after the FrameState
     *  snapshot and before the RenderGraph flip.
     *
     *  GPU implementations push an IRMapFadeCmd into the graph's write-side
     *  post-process buffers so the render thread can composite the wipe quad via
     *  ExecuteFromIR().  When no transition is active the implementation clears
     *  the map_fade slot.
     *
     *  Software implementations leave this as a no-op — they write directly
     *  to WScreen inside StepFadeIn/StepFadeOut. */
    virtual void FlushToRenderGraph(RenderGraph& /*graph*/) {}

    /** Called by RendererOpenGL::EndFrame_GL() (render thread) when the
     *  RenderGraph has a map-fade command for this frame.
     *
     *  GPU implementations capture the parchment view (if cmd.capture_pending)
     *  and draw the wipe quad using the previously-captured world view.
     *  Software implementations leave this as a no-op. */
    virtual void ExecuteFromIR(const IRMapFadeCmd& /*cmd*/) {}

    /** Called by RendererOpenGL::EndFrame_GL() (render thread) AFTER GameUI
     *  has drawn for this frame, on a frame where ExecuteFromIR() flagged a
     *  pending world-view capture. GPU implementations blit the now-complete
     *  default framebuffer (world geometry + GameUI) into the world-view
     *  snapshot texture, so the sidebar crossfades with the rest of the view
     *  during the transition instead of popping in/out abruptly once it
     *  completes. No-op if no capture is pending (the common case — capture
     *  only happens once per transition). Software implementations leave
     *  this as a no-op. */
    virtual void CaptureWorldFrameIfPending() {}

    /** Returns true when this pass can run the transition at any resolution,
     *  not just the original 320×200.  Software returns false; GPU returns true. */
    virtual bool SupportsNativeResolution() const { return false; }
};

/******************************************************************************/
