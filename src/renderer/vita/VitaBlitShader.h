/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaBlitShader.h
 *     Full-screen palette-blit shader program for the Vita renderer.
 * @par Purpose:
 *     Wraps the 8-bit palette-indexed blit shader:
 *       index_tex   (GL_LUMINANCE 640×480) → palette_tex (GL_RGBA 256×1) → RGBA
 *     Used in RendererVita::EndFrame() to decode lbDrawSurface to a scene FBO
 *     (Phase B+) or directly to the screen (Phase A).
 */
/******************************************************************************/
#ifndef VITA_BLIT_SHADER_H
#define VITA_BLIT_SHADER_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>

/**
 * Palette-blit fullscreen shader (GLSL, compiled at runtime via vitaGL).
 *
 * Attribute slots:
 *   0 — aPos  (vec2 NDC position)
 *   1 — aUV   (vec2 texture coordinate)
 *
 * Uniforms:
 *   indexTex   (sampler2D, unit 0) — 8-bit palette indices (GL_LUMINANCE)
 *   paletteTex (sampler2D, unit 1) — 256-entry RGBA palette
 */
class VitaBlitShader {
public:
    VitaBlitShader() = default;
    ~VitaBlitShader() { Free(); }

    VitaBlitShader(const VitaBlitShader&) = delete;
    VitaBlitShader& operator=(const VitaBlitShader&) = delete;

    /** Compile and link GLSL shaders, cache uniform locations. */
    bool Init();

    /** Activate this program (glUseProgram). */
    void Bind() const;

    /** Release all GL resources. */
    void Free();

    bool IsReady() const { return m_prog != 0; }

private:
    GLuint m_prog       = 0;
    GLint  m_loc_index  = -1;
    GLint  m_loc_palette = -1;
};

#endif // PLATFORM_VITA
#endif // VITA_BLIT_SHADER_H
