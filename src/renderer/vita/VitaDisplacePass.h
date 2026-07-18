/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaDisplacePass.h
 *     GPU post-process pass implementing the image displacement / warp effect.
 *
 *     Implements all three CPU-side DisplacementAlgorithm variants analytically
 *     in GLSL:
 *       Linear      — simple half-size zoom toward centre.
 *       Sinusoidal  — cross-axis sine-wave distortion (the default lens).
 *       Radial      — radial barrel / pincushion warp.
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/IPostProcessPass.h"
// Note: DisplacementAlgorithm is stored as int to avoid a circular include with
// DisplacementEffect.h.  Values: Linear=0, Sinusoidal=1, Radial=2.

class VitaDisplacePass : public IPostProcessPass {
public:
    VitaDisplacePass() = default;
    ~VitaDisplacePass() override { Free(); }

    /** Compile the default (Sinusoidal) shader variant. Does not require
     *  Configure() first — Configure() rebuilds the shader if it selects a
     *  different algorithm than the one currently compiled. */
    bool Init()  override;

    /** Supply algorithm parameters.
     *  Safe to call any time after a successful Init(); rebuilds the
     *  compiled shader if the algorithm differs from the current one.
     *  @param algo      DisplacementAlgorithm value (0=Linear, 1=Sinusoidal, 2=Radial).
     *  @param magnitude Displacement magnitude in reference 640×480 pixels.
     *  @param period    Warp period / scale factor (algorithm-specific). */
    void Configure(int algo, int magnitude, int period);
    void Configure(const LensGPUPassParams& params) override;

    void Apply(unsigned int src_tex, unsigned int dst_fbo,
               int src_w, int src_h) override;
    void Free()  override;

    bool IsInitialized() const { return m_program != 0; }

private:
    /** (Re)compile the shader for m_algo. Returns false on shader error. */
    bool BuildProgram();

    GLuint m_program   = 0;
    GLint  m_loc_scene = -1;
    GLint  m_loc_mag   = -1;
    GLint  m_loc_per   = -1;

    int  m_algo      = 1;  // 0=Linear, 1=Sinusoidal, 2=Radial
    int  m_magnitude = 0;
    int  m_period    = 1;
};

#endif // PLATFORM_VITA
