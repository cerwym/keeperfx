/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaPassthroughPass.cpp
 *     Simple RGBA passthrough blit — samples sceneTex and outputs it unchanged.
 *     Used for the final stage-3 blit (last ping-pong FBO → Vita screen).
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaPassthroughPass.h"

#ifdef PLATFORM_VITA

#include "renderer/vita/VitaPassCommon.h"
#include "post_inc.h"

static const char* k_passthrough_frag_glsl =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uScene, vUV);\n"
    "}\n";

bool VitaPassthroughPass::Init()
{
    m_program = vita_build_pass_program(k_passthrough_frag_glsl);
    if (!m_program) return false;

    glUseProgram(m_program);
    m_loc_scene = glGetUniformLocation(m_program, "uScene");
    glUniform1i(m_loc_scene, 0);   // always unit 0
    glUseProgram(0);

    SYNCLOG("VitaPassthroughPass: initialized");
    return true;
}

void VitaPassthroughPass::Apply(GLuint src_tex, GLuint dst_fbo, int src_w, int src_h)
{
    if (!m_program) return;

    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    if (dst_fbo == 0) {
        // Final blit to Vita screen — stretch to native resolution.
        glViewport(0, 0, k_screenW, k_screenH);
    } else {
        glViewport(0, 0, src_w, src_h);
    }

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src_tex);

    vita_draw_fullscreen_quad();
}

void VitaPassthroughPass::Free()
{
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_loc_scene = -1;
}

#endif // PLATFORM_VITA
