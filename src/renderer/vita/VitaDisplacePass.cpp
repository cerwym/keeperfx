/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaDisplacePass.cpp
 *     GPU displacement / warp post-process pass.
 *
 *     Three separate fragment shaders (one per algorithm) are compiled at
 *     init time so the Cg translator gets deterministic, branch-free code.
 *     The appropriate shader is selected by Configure() and compiled in Init().
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaDisplacePass.h"

#ifdef PLATFORM_VITA

#include "renderer/vita/VitaPassCommon.h"
#include "post_inc.h"

// ---------------------------------------------------------------------------
// Fragment shaders — one per algorithm
// Reference space: 640×480, centre at (320, 240).
// uMag = magnitude in reference pixels, uPer = period factor.
// ---------------------------------------------------------------------------

// Linear: map each pixel to halfway between its position and the centre.
static const char* k_displace_frag_linear =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "void main() {\n"
    "    vec2 refPx = vUV * vec2(640.0, 480.0);\n"
    "    vec2 src = (refPx + vec2(320.0, 240.0)) * 0.5;\n"
    "    gl_FragColor = texture2D(uScene, src / vec2(640.0, 480.0));\n"
    "}\n";

// Sinusoidal: cross-axis sine-wave warp.
static const char* k_displace_frag_sinusoidal =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "uniform float uMag;\n"
    "uniform float uPer;\n"
    "void main() {\n"
    "    vec2 refPx  = vUV * vec2(640.0, 480.0);\n"
    "    vec2 rel    = refPx - vec2(320.0, 240.0);\n"
    "    float src_x = sin(rel.y / 640.0 * uPer) * uMag + rel.x + 320.0;\n"
    "    float src_y = sin(rel.x / 480.0 * uPer) * uMag + rel.y + 240.0;\n"
    "    vec2 srcUV  = clamp(vec2(src_x, src_y), vec2(0.0), vec2(639.0, 479.0))\n"
    "                  / vec2(640.0, 480.0);\n"
    "    gl_FragColor = texture2D(uScene, srcUV);\n"
    "}\n";

// Radial: barrel / pincushion warp based on distance from centre.
static const char* k_displace_frag_radial =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "uniform float uMag;\n"
    "uniform float uPer;\n"
    "void main() {\n"
    "    vec2 refPx  = vUV * vec2(640.0, 480.0);\n"
    "    vec2 center = vec2(320.0, 240.0);\n"
    "    vec2 rel    = refPx - center;\n"
    "    float mag_sq = uMag * uMag;\n"
    "    float divs   = sqrt(240.0*240.0 + 320.0*320.0 + mag_sq);\n"
    "    float dist   = sqrt(dot(rel, rel) + mag_sq) / divs;\n"
    "    vec2 src     = dist * rel + center;\n"
    "    src = clamp(src, vec2(0.0), vec2(639.0, 479.0));\n"
    "    gl_FragColor = texture2D(uScene, src / vec2(640.0, 480.0));\n"
    "}\n";

// ---------------------------------------------------------------------------

void VitaDisplacePass::Configure(int algo, int magnitude, int period)
{
    m_algo       = algo;
    m_magnitude  = magnitude;
    m_period     = period;
    m_configured = true;
}

bool VitaDisplacePass::Init()
{
    if (!m_configured) return false;

    const char* frag = nullptr;
    switch (m_algo) {
        case 0: frag = k_displace_frag_linear;     break;  // DisplaceAlgo_Linear
        case 1: frag = k_displace_frag_sinusoidal; break;  // DisplaceAlgo_Sinusoidal
        case 2: frag = k_displace_frag_radial;     break;  // DisplaceAlgo_Radial
        default:
            ERRORLOG("VitaDisplacePass: unsupported algorithm %d", m_algo);
            return false;
    }

    m_program = vita_build_pass_program(frag);
    if (!m_program) return false;

    glUseProgram(m_program);
    m_loc_scene = glGetUniformLocation(m_program, "uScene");
    m_loc_mag   = glGetUniformLocation(m_program, "uMag");
    m_loc_per   = glGetUniformLocation(m_program, "uPer");
    glUniform1i(m_loc_scene, 0);
    if (m_loc_mag >= 0) glUniform1f(m_loc_mag, (float)m_magnitude);
    if (m_loc_per >= 0) glUniform1f(m_loc_per, (float)m_period);
    glUseProgram(0);

    SYNCLOG("VitaDisplacePass: initialized (algo=%d mag=%d per=%d)",
            (int)m_algo, m_magnitude, m_period);
    return true;
}

void VitaDisplacePass::Apply(unsigned int src_tex, unsigned int dst_fbo,
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

void VitaDisplacePass::Free()
{
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_loc_scene = m_loc_mag = m_loc_per = -1;
    m_configured = false;
}

#endif // PLATFORM_VITA
