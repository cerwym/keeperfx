/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaShaderProgram.cpp
 *     RAII wrapper around a vitaGL pre-compiled shader program.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/VitaShaderProgram.h"

#ifdef PLATFORM_VITA

#include <stdio.h>
#include <stdint.h>
#include "globals.h"
#include "post_inc.h"

// ---------------------------------------------------------------------------
// vitaGL's glShaderBinary / unserialize_shader format:
//   [uint32 matrix_uniforms_num] [index * N] [raw GXP data]
//
// Raw .gxp files start with the magic "GXP\0" which vitaGL would misread as
// matrix_uniforms_num ≈ 5 million, causing a massive bounds overrun.
// Prepending a zero uint32 tells unserialize_shader there are no matrix
// uniforms; it then skips exactly 4 bytes and interprets the rest as the GXP
// payload correctly.
// ---------------------------------------------------------------------------
static GLuint load_shader_binary(GLenum type, const char* path)
{
    GLuint sh = glCreateShader(type);
    FILE* f = fopen(path, "rb");
    if (!f) {
        ERRORLOG("VitaShaderProgram: cannot open shader binary: %s", path);
        glDeleteShader(sh);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        ERRORLOG("VitaShaderProgram: shader binary is empty: %s", path);
        fclose(f);
        glDeleteShader(sh);
        return 0;
    }
    size_t total = sizeof(uint32_t) + (size_t)sz;
    void* buf = KfxAlloc(total);
    if (!buf) {
        ERRORLOG("VitaShaderProgram: OOM allocating %zu bytes for %s", total, path);
        fclose(f);
        glDeleteShader(sh);
        return 0;
    }
    *(uint32_t*)buf = 0; // matrix_uniforms_num = 0
    fread((uint8_t*)buf + sizeof(uint32_t), 1, (size_t)sz, f);
    fclose(f);
    glShaderBinary(1, &sh, 0, buf, (GLsizei)total);
    KfxFree(buf);
    return sh;
}

bool VitaShaderProgram::Load(GLenum type, const char* gxp_path)
{
    GLuint sh = load_shader_binary(type, gxp_path);
    if (!sh) return false;
    if (type == GL_VERTEX_SHADER) {
        if (m_vert) glDeleteShader(m_vert);
        m_vert = sh;
    } else {
        if (m_frag) glDeleteShader(m_frag);
        m_frag = sh;
    }
    return true;
}

void VitaShaderProgram::BindAttrib(GLuint index, const char* name)
{
    if (m_program) {
        glBindAttribLocation(m_program, index, name);
    } else {
        // Defer: store and apply during Link()
        // For simplicity we create the program object here so that
        // BindAttrib can be called before Link().
        if (!m_program) m_program = glCreateProgram();
        glBindAttribLocation(m_program, index, name);
    }
}

bool VitaShaderProgram::Link(const char* debug_name)
{
    if (!m_vert || !m_frag) {
        ERRORLOG("VitaShaderProgram::Link(%s): vertex or fragment shader not loaded", debug_name);
        return false;
    }
    if (!m_program) m_program = glCreateProgram();
    glAttachShader(m_program, m_vert);
    glAttachShader(m_program, m_frag);
    glLinkProgram(m_program);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        ERRORLOG("VitaShaderProgram::Link(%s): program failed to link", debug_name);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    { GLenum e = glGetError(); if (e) ERRORLOG("VitaShaderProgram::Link(%s): GL error 0x%x", debug_name, e); }
    return true;
}

void VitaShaderProgram::Bind() const
{
    glUseProgram(m_program);
}

GLint VitaShaderProgram::GetUniform(const char* name) const
{
    return glGetUniformLocation(m_program, name);
}

void VitaShaderProgram::Free()
{
    if (m_program) { glDeleteProgram(m_program);    m_program = 0; }
    if (m_vert)    { glDeleteShader(m_vert);         m_vert    = 0; }
    if (m_frag)    { glDeleteShader(m_frag);         m_frag    = 0; }
}

#endif // PLATFORM_VITA
