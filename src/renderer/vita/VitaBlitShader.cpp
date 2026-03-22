/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaBlitShader.cpp
 *     Full-screen palette-blit shader program for the Vita renderer.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaBlitShader.h"

#ifdef PLATFORM_VITA

#include "globals.h"
#include "renderer/vita/VitaPassCommon.h"
#include "post_inc.h"

// ---------------------------------------------------------------------------
// Palette-decode blit: samples 8-bit indexed framebuffer (GL_LUMINANCE) and
// looks up each index in a 256×1 RGBA palette texture.
// ---------------------------------------------------------------------------
static const char* k_blit_frag_glsl =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D indexTex;\n"
    "uniform sampler2D paletteTex;\n"
    "void main() {\n"
    "    float idx = texture2D(indexTex, vUV).r;\n"
    "    gl_FragColor = texture2D(paletteTex, vec2(idx, 0.5));\n"
    "}\n";

bool VitaBlitShader::Init()
{
    m_prog = vita_build_pass_program(k_blit_frag_glsl);
    if (!m_prog) return false;

    glUseProgram(m_prog);
    m_loc_index   = glGetUniformLocation(m_prog, "indexTex");
    m_loc_palette = glGetUniformLocation(m_prog, "paletteTex");
    glUniform1i(m_loc_index,   0);   // indexTex  → unit 0
    glUniform1i(m_loc_palette, 1);   // paletteTex → unit 1
    glUseProgram(0);

    SYNCLOG("VitaBlitShader: initialised (GLSL, indexTex unit=0 paletteTex unit=1)");
    return true;
}

void VitaBlitShader::Bind() const
{
    glUseProgram(m_prog);
}

void VitaBlitShader::Free()
{
    if (m_prog) { glDeleteProgram(m_prog); m_prog = 0; }
    m_loc_index   = -1;
    m_loc_palette = -1;
}

#endif // PLATFORM_VITA
