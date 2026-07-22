/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaLensRenderer.h
 *     Vita (vitaGL) implementation of ILensRenderer.
 * @par Purpose:
 *     Owns the *entire* Vita realization of the lens system, mirroring
 *     GLLensRenderer:
 *
 *       - the concrete Vita lens passes (VitaMistPass, ...) cached by effect type;
 *       - the offscreen compositing FBOs (scene + ping-pong pair) the decoded
 *         world is rendered into when a geometric lens is active;
 *       - the stage-3 passthrough blit that upscales the final lens texture to
 *         the 960×544 Vita screen.
 *
 *     RendererVita::EndFrame() drives it via BeginSceneCapture() → (palette
 *     decode blit) → ResolveAndApply(). Pass ownership lives in the backend, not
 *     in the game-side LensEffect. The game side stays pure data (IRLensCmd).
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include <vitaGL.h>

#include <vector>

#include "renderer/ILensRenderer.h"
#include "renderer/IPostProcessPass.h"          // LensGPUPassParams
#include "kfx/lense/LensEffect.h"               // LensEffectType
#include "renderer/vita/VitaPassthroughPass.h"  // stage-3 final blit

struct IRLensCmd;
struct IRLensEffect;

/******************************************************************************/

class VitaLensRenderer : public ILensRenderer {
public:
    ~VitaLensRenderer() override;

    const char* GetName() const override { return "VitaLensRenderer"; }

    /** Create the scene + ping-pong compositing FBOs and the passthrough blit
     *  program for w×h. Returns false (and frees anything created) on failure. */
    bool Init(int w, int h);

    /** Free the compositing FBOs, passthrough program and cached passes. */
    void Shutdown();

    // ── Frame compositing (driven by RendererVita::EndFrame) ─────────────────

    /** Select and bind the stage-1 palette-decode target. When the command has
     *  GPU passes, binds the scene FBO (viewport w×h) and returns true; otherwise
     *  binds the default framebuffer (viewport 960×544) and returns false. The
     *  caller then draws the palette-decode blit into the bound target. */
    bool BeginSceneCapture(const IRLensCmd& cmd);

    /** Run the ping-pong GPU pass chain over the decoded scene and blit the
     *  final result to the Vita screen. Pairs with a BeginSceneCapture() that
     *  returned true. */
    void ResolveAndApply(const IRLensCmd& cmd);

    void ReleaseAll();

private:
    static constexpr int kSlotCount = 8;

    struct Slot {
        IPostProcessPass* pass       = nullptr;
        LensGPUPassParams last_params;             // pointer fields always null (compare key)
        std::vector<unsigned char> last_mist;      // last configured mist payload
        std::vector<unsigned char> last_overlay;   // last configured overlay payload
        bool              configured = false;
    };

    static IPostProcessPass* CreatePass(LensEffectType type);

    /** Cached, configured pass for `effect` (internal compositing detail).
     *  Resolves the effect's owned mist/overlay payload into the pass config, so
     *  no game-thread pointer is dereferenced. */
    IPostProcessPass* AcquireConfiguredPass(const IRLensEffect& effect);

    Slot m_slots[kSlotCount];

    // ── Compositing FBOs (owned) ─────────────────────────────────────────────
    GLuint m_scene_fbo  = 0;   /**< decoded RGBA w×h scene render target */
    GLuint m_scene_tex  = 0;
    GLuint m_pass_fbo_a = 0;   /**< ping-pong FBO A */
    GLuint m_pass_tex_a = 0;
    GLuint m_pass_fbo_b = 0;   /**< ping-pong FBO B */
    GLuint m_pass_tex_b = 0;
    VitaPassthroughPass m_passthrough; /**< stage-3 final blit to screen (960×544) */

    int m_fbo_w = 0;
    int m_fbo_h = 0;
};

/******************************************************************************/
#endif // PLATFORM_VITA
