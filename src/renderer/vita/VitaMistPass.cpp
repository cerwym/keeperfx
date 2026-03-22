/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaMistPass.cpp
 *     GPU mist / fog post-process pass implementation.
 *
 * Algorithm (matches the CPU CMistFade::Render):
 *   primary_uv  = fract( (pos  + screen_uv * REF_SCALE) / 256 )
 *   secondary_uv = fract( (sec  - swapped screen_uv * REF_SCALE) / 256 )
 *   k   = mist_tex(primary_uv).r        → [0, 1] representing [0, 255]
 *   i   = mist_tex(secondary_uv).r
 *   fog = clamp( (k + i) * 255 / 256, 0, 1 )  ← matches (k_byte+i_byte)>>3÷32
 *   out = scene_color * (1 - fog)
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaMistPass.h"

#ifdef PLATFORM_VITA

#include "renderer/vita/VitaPassCommon.h"
#include "post_inc.h"

// ---------------------------------------------------------------------------
// Shader — fragment
// REF scale factors: 640/256 = 2.5, 480/256 = 1.875
// uPos / uSec are the normalised (÷256) animation offsets.
// ---------------------------------------------------------------------------
static const char* k_mist_frag_glsl =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uMist;\n"
    "uniform vec2 uPos;\n"    // primary offset [0,1]
    "uniform vec2 uSec;\n"    // secondary offset [0,1]
    "void main() {\n"
    // Primary layer: texel_xy = (pos + virtual_xy) / 256  (wrapped)
    // virtual_x = vUV.x * 640, virtual_y = vUV.y * 480
    // texel_x = fract(uPos.x + vUV.x * 2.5)
    "    vec2 uv1 = fract(uPos + vec2(vUV.x * 2.5, vUV.y * 1.875));\n"
    // Secondary layer: texel_xy = (sec_x - virtual_y, sec_y - virtual_x) / 256
    "    vec2 uv2 = fract(vec2(uSec.x - vUV.y * 1.875, uSec.y - vUV.x * 2.5));\n"
    "    float k = texture2D(uMist, uv1).r;\n"
    "    float i = texture2D(uMist, uv2).r;\n"
    // Fog density: (k_byte + i_byte) / 8, clamped to 32 → normalise to [0,1]
    // = (k + i) * 255.0 / 8.0 / 32.0 = (k + i) * 255.0 / 256.0
    "    float fog = clamp((k + i) * 255.0 / 256.0, 0.0, 1.0);\n"
    "    vec4 sc = texture2D(uScene, vUV);\n"
    "    gl_FragColor = vec4(sc.rgb * (1.0 - fog), sc.a);\n"
    "}\n";

// ---------------------------------------------------------------------------

void VitaMistPass::Configure(const unsigned char* data,
                             int pos_x_step, int pos_y_step,
                             int sec_x_step, int sec_y_step)
{
    // Copy step values; mist data will be used in Init().
    m_step_pos_x = pos_x_step;
    m_step_pos_y = pos_y_step;
    m_step_sec_x = sec_x_step;
    m_step_sec_y = sec_y_step;
    m_configured = (data != nullptr);

    // Store pointer for Init(); the caller (MistEffect::Setup) guarantees
    // that eye_lens_memory remains valid until Init() is called in the same
    // Setup() invocation.
    if (data) {
        // Upload later in Init() via the pointer passed here.
        // Keep a static pointer — valid only during Setup().
        m_pending_data = data;
    }
}

bool VitaMistPass::Init()
{
    if (!m_configured || !m_pending_data) return false;

    // Upload the 256×256 mist amplitude texture as GL_LUMINANCE.
    glGenTextures(1, &m_mist_tex);
    glBindTexture(GL_TEXTURE_2D, m_mist_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 256, 0,
                 GL_LUMINANCE, GL_UNSIGNED_BYTE, m_pending_data);
    m_pending_data = nullptr;

    m_program = vita_build_pass_program(k_mist_frag_glsl);
    if (!m_program) { Free(); return false; }

    glUseProgram(m_program);
    m_loc_scene = glGetUniformLocation(m_program, "uScene");
    m_loc_mist  = glGetUniformLocation(m_program, "uMist");
    m_loc_pos   = glGetUniformLocation(m_program, "uPos");
    m_loc_sec   = glGetUniformLocation(m_program, "uSec");
    glUniform1i(m_loc_scene, 0);
    glUniform1i(m_loc_mist,  1);
    glUseProgram(0);

    SYNCLOG("VitaMistPass: initialized");
    return true;
}

void VitaMistPass::Tick()
{
    // Advance animation offsets (byte arithmetic, matching CMistFade::Animate).
    m_pos_x = (m_pos_x + m_step_pos_x) & 0xFF;
    m_pos_y = (m_pos_y + m_step_pos_y) & 0xFF;
    // Secondary X is subtracted (sec_x_step = 253 ≡ -3 in byte arithmetic).
    m_sec_x = (m_sec_x - m_step_sec_x) & 0xFF;
    m_sec_y = (m_sec_y + m_step_sec_y) & 0xFF;
}

void VitaMistPass::Apply(unsigned int src_tex, unsigned int dst_fbo,
                         int src_w, int src_h)
{
    if (!m_program) return;

    Tick();

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)dst_fbo);
    glViewport(0, 0, src_w, src_h);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)src_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_mist_tex);

    // Normalise byte offsets to [0,1] for the shader.
    glUniform2f(m_loc_pos, m_pos_x / 256.0f, m_pos_y / 256.0f);
    glUniform2f(m_loc_sec, m_sec_x / 256.0f, m_sec_y / 256.0f);

    vita_draw_fullscreen_quad();
}

void VitaMistPass::Free()
{
    if (m_program)  { glDeleteProgram(m_program);  m_program  = 0; }
    if (m_mist_tex) { glDeleteTextures(1, &m_mist_tex); m_mist_tex = 0; }
    m_loc_scene = m_loc_mist = m_loc_pos = m_loc_sec = -1;
    m_configured  = false;
    m_pending_data = nullptr;
}

#endif // PLATFORM_VITA
