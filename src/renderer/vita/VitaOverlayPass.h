/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaOverlayPass.h
 *     GPU post-process pass implementing the palette-indexed overlay composite.
 *
 *     The overlay image is uploaded once as GL_LUMINANCE (8-bit palette indices).
 *     Each frame the current lbPalette is used to decode indices to RGBA, and the
 *     decoded overlay is alpha-blended over the decoded scene.  Palette index 255
 *     is treated as transparent (matching the CPU path).
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/IPostProcessPass.h"

class VitaOverlayPass : public IPostProcessPass {
public:
    VitaOverlayPass() = default;
    ~VitaOverlayPass() override { Free(); }

    /** Compile the shader and allocate the (dimension-independent) palette
     *  texture. Does not require Configure() first — the overlay texture is
     *  allocated and uploaded on the first Configure() call. */
    bool Init()  override;

    /** Supply overlay image data and blend factor.
     *  Safe to call any time after a successful Init().
     *  @param data  Pointer to width×height bytes of 8-bit palette indices.
     *  @param w     Overlay texture width in pixels.
     *  @param h     Overlay texture height in pixels.
     *  @param alpha Blend factor in [0, 1] (0 = fully transparent overlay). */
    void Configure(const unsigned char* data, int w, int h, float alpha);
    void Configure(const LensGPUPassParams& params) override;

    void Apply(unsigned int src_tex, unsigned int dst_fbo,
               int src_w, int src_h) override;
    void Free()  override;

    bool IsInitialized() const { return m_program != 0; }

private:
    GLuint m_program     = 0;
    GLuint m_overlay_tex = 0;  // GL_LUMINANCE  w×h  (static, uploaded once)
    GLuint m_palette_tex = 0;  // GL_RGBA        256×1 (updated each frame from lbPalette)

    GLint m_loc_scene   = -1;
    GLint m_loc_overlay = -1;
    GLint m_loc_palette = -1;
    GLint m_loc_alpha   = -1;

    float m_alpha = 0.5f;

    int   m_overlay_w   = 0;
    int   m_overlay_h   = 0;
    bool  m_configured  = false; // true once real overlay data has been uploaded
};

#endif // PLATFORM_VITA
