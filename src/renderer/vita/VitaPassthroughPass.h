/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaPassthroughPass.h
 *     Simple RGBA passthrough blit used as the final stage-3 blit from the
 *     last ping-pong FBO to the screen (FBO 0).
 *
 *     Not a LensEffect GPU pass — owned directly by RendererVita.
 */
/******************************************************************************/
#ifndef VITA_PASSTHROUGH_PASS_H
#define VITA_PASSTHROUGH_PASS_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>

class VitaPassthroughPass {
public:
    VitaPassthroughPass() = default;
    ~VitaPassthroughPass() { Free(); }

    /** Compile the passthrough GLSL program. */
    bool Init();

    /** Blit src_tex to dst_fbo.
     *  When dst_fbo == 0, renders to the Vita screen (viewport 960×544).
     *  When dst_fbo != 0, renders to that FBO at src_w × src_h. */
    void Apply(GLuint src_tex, GLuint dst_fbo, int src_w, int src_h);

    /** Release the GL program. */
    void Free();

    bool IsInitialized() const { return m_program != 0; }

private:
    GLuint m_program   = 0;
    GLint  m_loc_scene = -1;

    static const int k_screenW = 960;
    static const int k_screenH = 544;
};

#endif // PLATFORM_VITA
#endif // VITA_PASSTHROUGH_PASS_H
