/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaOverlayPass.cpp
 *     GPU post-process pass: palette-indexed overlay composite.
 */
/******************************************************************************/
#ifdef PLATFORM_VITA

#include "renderer/vita/VitaOverlayPass.h"
#include "renderer/vita/VitaPassCommon.h"
#include "bflib_video.h"   // lbPalette (unsigned char[768], 6-bit RGB triplelets)

// ---------------------------------------------------------------------------
// Fragment shader
// ---------------------------------------------------------------------------
// uOverlay  : GL_LUMINANCE  texture carrying 8-bit palette indices.
// uPalette  : GL_RGBA 256×1 texture; uploaded from lbPalette each frame.
// Palette index 255 is the transparent "key" colour — pass scene through.
// ---------------------------------------------------------------------------
static const char k_overlay_frag[] =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uOverlay;\n"
    "uniform sampler2D uPalette;\n"
    "uniform float uAlpha;\n"
    "void main(void) {\n"
    "    vec4 scene = texture2D(uScene, vUV);\n"
    "    float idx  = texture2D(uOverlay, vUV).r;\n"
    "    if (idx > (254.5 / 255.0)) {\n"
    "        gl_FragColor = scene;\n"
    "        return;\n"
    "    }\n"
    "    vec4 col = texture2D(uPalette, vec2(idx + 0.5 / 256.0, 0.5));\n"
    "    gl_FragColor = mix(scene, col, uAlpha);\n"
    "}\n";

// ---------------------------------------------------------------------------
bool VitaOverlayPass::Init()
{
    m_program = vita_build_pass_program(k_overlay_frag);
    if (!m_program)
        return false;

    m_loc_scene   = glGetUniformLocation(m_program, "uScene");
    m_loc_overlay = glGetUniformLocation(m_program, "uOverlay");
    m_loc_palette = glGetUniformLocation(m_program, "uPalette");
    m_loc_alpha   = glGetUniformLocation(m_program, "uAlpha");

    // Allocate palette texture (256×1 RGBA, filled each frame). Dimension-
    // independent, so this can happen before Configure() supplies the
    // overlay image.
    glGenTextures(1, &m_palette_tex);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

// ---------------------------------------------------------------------------
void VitaOverlayPass::Configure(const unsigned char* data, int w, int h, float alpha)
{
    m_alpha = alpha;

    if (data == nullptr || m_program == 0)
        return;

    // Validate dimensions before using them (max 4096×4096)
    if (w <= 0 || w > 4096 || h <= 0 || h > 4096) {
        ERRORLOG("VitaOverlayPass: invalid dimensions w=%d h=%d (must be 1-4096)", w, h);
        return;
    }

    if (!m_overlay_tex) {
        glGenTextures(1, &m_overlay_tex);
        glBindTexture(GL_TEXTURE_2D, m_overlay_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_2D, m_overlay_tex);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h,
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_overlay_w  = w;
    m_overlay_h  = h;
    m_configured = true;
}

void VitaOverlayPass::Configure(const LensGPUPassParams& params)
{
    Configure(params.overlay_data, params.overlay_w, params.overlay_h, params.overlay_alpha);
}

// ---------------------------------------------------------------------------
void VitaOverlayPass::Apply(unsigned int src_tex, unsigned int dst_fbo,
                             int src_w, int src_h)
{
    if (!m_program || !m_configured)
        return;

    // Rebuild the palette texture from the current active 6-bit RGB palette.
    // Layout: [r0,g0,b0, r1,g1,b1, ...] each value 0‥63 (×4 → 0‥255).
    const unsigned char* pal = LbPaletteGetReadonly();
    unsigned char rgba_pal[256 * 4];
    for (int i = 0; i < 256; ++i) {
        rgba_pal[i * 4 + 0] = (unsigned char)(pal[i * 3 + 0] << 2);
        rgba_pal[i * 4 + 1] = (unsigned char)(pal[i * 3 + 1] << 2);
        rgba_pal[i * 4 + 2] = (unsigned char)(pal[i * 3 + 2] << 2);
        rgba_pal[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba_pal);

    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(0, 0, src_w, src_h);

    glUseProgram(m_program);

    glUniform1i(m_loc_scene,   0);
    glUniform1i(m_loc_overlay, 1);
    glUniform1i(m_loc_palette, 2);
    glUniform1f(m_loc_alpha,   m_alpha);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)src_tex);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_overlay_tex);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_palette_tex);

    vita_draw_fullscreen_quad();

    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
void VitaOverlayPass::Free()
{
    if (m_palette_tex) { glDeleteTextures(1, &m_palette_tex); m_palette_tex = 0; }
    if (m_overlay_tex) { glDeleteTextures(1, &m_overlay_tex); m_overlay_tex = 0; }
    if (m_program)     { glDeleteProgram(m_program);           m_program     = 0; }
    m_configured = false;
}

#endif // PLATFORM_VITA
