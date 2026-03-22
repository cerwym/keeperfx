/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaFlyeyePass.cpp
 *     GPU compound-eye / flyeye post-process pass.
 *
 * Algorithm (approximates CPU FlyeyeEffect):
 *   Hex grid: columns 50px wide, rows 60px tall, odd columns staggered +30px.
 *   Each hex at (col, row) shows the scene shifted by (-col, -row) × ldpar1
 *   in reference (640×480) pixel space, where ldpar1 = 640 × 0.0175 = 11.2.
 *
 *   This matches the CPU path's source_strip_w/h offsets per hex cell.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaFlyeyePass.h"

#ifdef PLATFORM_VITA

#include "renderer/vita/VitaPassCommon.h"
#include "post_inc.h"

static const char* k_flyeye_frag_glsl =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "void main() {\n"
    // Map vUV to reference pixel coordinates (640×480).
    "    vec2 refPx = vUV * vec2(640.0, 480.0);\n"
    // Hex column (50px wide) and stagger for odd columns.
    "    float col = floor(refPx.x / 50.0);\n"
    "    float odd = mod(col, 2.0);\n"
    // Hex row (60px tall, offset by 30px for odd columns).
    "    float row = floor((refPx.y - odd * 30.0) / 60.0);\n"
    // Per-hex source offset: (-col, -row) * ldpar1  (ldpar1 = 640 * 0.0175 = 11.2)
    "    vec2 off = vec2(-col, -row) * 11.2;\n"
    // Sample scene at shifted position, clamped to valid range.
    "    vec2 srcPx  = clamp(refPx + off, vec2(0.0), vec2(639.0, 479.0));\n"
    "    vec2 srcUV  = srcPx / vec2(640.0, 480.0);\n"
    "    gl_FragColor = texture2D(uScene, srcUV);\n"
    "}\n";

bool VitaFlyeyePass::Init()
{
    m_program = vita_build_pass_program(k_flyeye_frag_glsl);
    if (!m_program) return false;

    glUseProgram(m_program);
    m_loc_scene = glGetUniformLocation(m_program, "uScene");
    glUniform1i(m_loc_scene, 0);
    glUseProgram(0);

    SYNCLOG("VitaFlyeyePass: initialized");
    return true;
}

void VitaFlyeyePass::Apply(unsigned int src_tex, unsigned int dst_fbo,
                           int src_w, int src_h)
{
    if (!m_program) return;

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)dst_fbo);
    glViewport(0, 0, src_w, src_h);

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)src_tex);

    vita_draw_fullscreen_quad();
}

void VitaFlyeyePass::Free()
{
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_loc_scene = -1;
}

#endif // PLATFORM_VITA
