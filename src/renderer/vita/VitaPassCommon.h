/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaPassCommon.h
 *     Shared utilities for GLSL-based post-process passes on Vita.
 * @par Purpose:
 *     Provides the shared full-screen quad geometry, a common vertex shader
 *     source string, and inline helpers for compiling / linking GLSL programs
 *     via vitaGL's runtime GLSL-to-GXM translator.
 *
 *     All helpers are inline / static so this header may be included in
 *     multiple translation units without linker conflicts.
 */
/******************************************************************************/
#ifndef VITA_PASS_COMMON_H
#define VITA_PASS_COMMON_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "globals.h"    // ERRORLOG, SYNCLOG

/******************************************************************************/
// Full-screen quad geometry
/******************************************************************************/

/** Clip-space positions for a full-screen triangle strip (TL,TR,BL,BR). */
static const float k_pp_pos[4][2] = {
    { -1.0f,  1.0f }, {  1.0f,  1.0f },
    { -1.0f, -1.0f }, {  1.0f, -1.0f },
};

/** UV coordinates for the full screen [0,1] (TL,TR,BL,BR, matching k_pp_pos). */
static const float k_pp_uv[4][2] = {
    { 0.0f, 0.0f }, { 1.0f, 0.0f },
    { 0.0f, 1.0f }, { 1.0f, 1.0f },
};

/******************************************************************************/
// Shared vertex shader
/******************************************************************************/

/** All post-process passes share this vertex shader.
 *  attrib 0 = aPos (vec2 clip-space), attrib 1 = aUV (vec2). */
static const char* k_pp_vert_glsl =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);\n"
    "    vUV = aUV;\n"
    "}\n";

/******************************************************************************/
// GLSL compilation helpers
/******************************************************************************/

/** Compile a single GLSL shader stage.
 *  @return GL shader object, or 0 on failure. */
static inline GLuint vita_compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    // Use explicit length instead of nullptr to prevent vitaGL from calling strlen()
    // on potentially corrupted pointers. strlen() reads unbounded memory.
    GLint len = src ? (GLint)strlen(src) : 0;
    glShaderSource(s, 1, &src, &len);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(s, (GLsizei)(sizeof(log) - 1), nullptr, log);
        ERRORLOG("[vitaGL] GLSL compile error: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

/** Link vert + frag into a GL program.
 *  Binds attrib 0="aPos" and attrib 1="aUV" before linking.
 *  Deletes vert and frag shaders regardless of success (they are consumed).
 *  @return GL program handle, or 0 on failure. */
static inline GLuint vita_link_program(GLuint vert, GLuint frag)
{
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return 0;
    }
    GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return 0;
    }
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glLinkProgram(prog);
    // vitaGL does not expose glDetachShader — shaders are freed directly.
    glDeleteShader(vert);
    glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetProgramInfoLog(prog, (GLsizei)(sizeof(log) - 1), nullptr, log);
        ERRORLOG("[vitaGL] GLSL link error: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/** Build a complete GLSL program from the shared vertex shader and a custom
 *  fragment shader source string.  Returns 0 on failure. */
static inline GLuint vita_build_pass_program(const char* frag_glsl)
{
    GLuint vert = vita_compile_shader(GL_VERTEX_SHADER,   k_pp_vert_glsl);
    GLuint frag = vita_compile_shader(GL_FRAGMENT_SHADER, frag_glsl);
    return vita_link_program(vert, frag);  // owns and deletes vert/frag
}

/******************************************************************************/
// Full-screen quad draw call
/******************************************************************************/

/** Draw a full-screen quad using the standard GL array path (works with
 *  GLSL runtime-compiled programs via glDrawArrays). */
static inline void vita_draw_fullscreen_quad()
{
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, k_pp_pos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, k_pp_uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

#endif // PLATFORM_VITA
#endif // VITA_PASS_COMMON_H
