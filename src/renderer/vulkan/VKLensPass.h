/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKLensPass.h
 *     Vulkan stub implementations of IPostProcessPass for lens effects.
 * @par Purpose:
 *     Phase-5a stubs: all Init() calls return false so the engine falls back
 *     to software lens processing.  Full VK pipeline implementations are
 *     deferred until the VK render-graph is operational (Phase 7).
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_VULKAN_ENABLED

#include "renderer/IPostProcessPass.h"

/******************************************************************************/

/** Vulkan stub base for lens passes.  Init() returns false → engine falls
 *  back to software lens processing.  All other methods are no-ops. */
class VKLensPassStub : public IPostProcessPass {
public:
    bool Init()  override { return false; }
    void Apply(unsigned int /*src_tex*/, unsigned int /*dst_fbo*/,
               int /*w*/, int /*h*/) override {}
    void Free()  override {}
};

/******************************************************************************/

class VKDisplacementPass : public VKLensPassStub {};
class VKMistPass         : public VKLensPassStub {};
class VKFlyeyePass       : public VKLensPassStub {};
class VKOverlayPass      : public VKLensPassStub {
public:
    void SetOverlayAlpha(float /*a*/) {}
    bool UploadOverlay(const unsigned char* /*rgba*/, int /*w*/, int /*h*/) { return false; }
};

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
