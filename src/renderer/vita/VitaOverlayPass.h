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
#ifndef VITA_OVERLAY_PASS_H
#define VITA_OVERLAY_PASS_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/IPostProcessPass.h"

class VitaOverlayPass : public IPostProcessPass {
public:
    VitaOverlayPass() = default;
    ~VitaOverlayPass() override { Free(); }

    /** Supply overlay image data and blend factor before Init().
     *  @param data  Pointer to width×height bytes of 8-bit palette indices.
     *  @param w     Overlay texture width in pixels.
     *  @param h     Overlay texture height in pixels.
     *  @param alpha Blend factor in [0, 1] (0 = fully transparent overlay). */
    void Configure(const unsigned char* data, int w, int h, float alpha);

    bool Init()  override;
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

    const unsigned char* m_pending_data = nullptr;
    int   m_pending_w   = 0;
    int   m_pending_h   = 0;
    bool  m_configured  = false;
};

#endif // PLATFORM_VITA
#endif // VITA_OVERLAY_PASS_H
