/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaMistPass.h
 *     GPU post-process pass implementing the animated mist / fog effect.
 *
 *     Two layers of a 256×256 amplitude texture are sampled at scrolling offsets
 *     and combined to produce a per-pixel fog density which darkens the scene.
 *     Matches the visual output of CMistFade::Render() in the CPU path.
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/IPostProcessPass.h"

class VitaMistPass : public IPostProcessPass {
public:
    VitaMistPass() = default;
    ~VitaMistPass() override { Free(); }

    /** Compile the shader and allocate an (empty) mist texture.
     *  Does not require Configure() to have been called first — the texture
     *  is populated with real data on the first Configure() call. */
    bool Init()  override;

    /** Supply the 256×256 mist amplitude data and per-frame step values.
     *  Safe to call any time after a successful Init() (including before
     *  the first Apply()).
     *  @param data       Pointer to 256×256 bytes of mist texture data.
     *  @param pos_x_step Primary-layer X offset added each frame (byte-wrapping).
     *  @param pos_y_step Primary-layer Y offset added each frame.
     *  @param sec_x_step Secondary-layer X offset subtracted each frame.
     *  @param sec_y_step Secondary-layer Y offset added each frame. */
    void Configure(const unsigned char* data,
                   int pos_x_step, int pos_y_step,
                   int sec_x_step, int sec_y_step);
    void Configure(const LensGPUPassParams& params) override;

    void Apply(unsigned int src_tex, unsigned int dst_fbo,
               int src_w, int src_h) override;
    void Free()  override;

    bool IsInitialized() const { return m_program != 0; }

private:
    /** Advance animation counters by one frame (called from Apply). */
    void Tick();

    GLuint m_program  = 0;
    GLuint m_mist_tex = 0;

    GLint m_loc_scene = -1;
    GLint m_loc_mist  = -1;
    GLint m_loc_pos   = -1;
    GLint m_loc_sec   = -1;

    // Animation state (byte-wrapped, normalised to [0,1] when uploading).
    int m_pos_x = 0, m_pos_y = 0;
    int m_sec_x = 50, m_sec_y = 128;  // defaults matching CMistFade

    // Per-frame step values (raw byte values, matching CMistFade convention).
    int m_step_pos_x = 2,   m_step_pos_y = 1;
    int m_step_sec_x = 253, m_step_sec_y = 3;

    bool m_configured = false; // true once real mist data has been uploaded
};

#endif // PLATFORM_VITA
