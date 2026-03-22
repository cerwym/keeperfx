/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaFlyeyePass.h
 *     GPU post-process pass implementing the compound-eye / flyeye effect.
 *
 *     Reproduces the hexagonal lens tiling from the CPU FlyeyeEffect using an
 *     analytical GLSL shader.  No external data is required — all parameters
 *     are taken directly from the original algorithm constants.
 */
/******************************************************************************/
#ifndef VITA_FLYEYE_PASS_H
#define VITA_FLYEYE_PASS_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/IPostProcessPass.h"

class VitaFlyeyePass : public IPostProcessPass {
public:
    VitaFlyeyePass() = default;
    ~VitaFlyeyePass() override { Free(); }

    bool Init()  override;
    void Apply(unsigned int src_tex, unsigned int dst_fbo,
               int src_w, int src_h) override;
    void Free()  override;

    bool IsInitialized() const { return m_program != 0; }

private:
    GLuint m_program   = 0;
    GLint  m_loc_scene = -1;
};

#endif // PLATFORM_VITA
#endif // VITA_FLYEYE_PASS_H
