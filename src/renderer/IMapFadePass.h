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
 * @par Future GPU implementation:
 *       StepFadeIn/Out → render-to-texture capture + UV-warp fragment shader
 */
/******************************************************************************/
#ifndef IMAP_FADE_PASS_H
#define IMAP_FADE_PASS_H

/******************************************************************************/

class IMapFadePass {
public:
    virtual ~IMapFadePass() = default;

    /** Render one fade-in frame (parchment → 3D view).
     *  On step == 0, captures the source and destination frames.
     *  @param step  Current step value in [0, 32].
     *  @return      Next step value (step + 4, capped at 32). */
    virtual long StepFadeIn(long step) = 0;

    /** Render one fade-out frame (3D view → parchment).
     *  On step == 32, captures the source and destination frames.
     *  @param step  Current step value in [0, 32].
     *  @return      Next step value (step - 4, capped at 0). */
    virtual long StepFadeOut(long step) = 0;

    virtual const char* GetName() const = 0;
};

/******************************************************************************/
#endif // IMAP_FADE_PASS_H
