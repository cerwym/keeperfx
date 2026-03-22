/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaShaderProgram.h
 *     RAII wrapper around a vitaGL pre-compiled shader program.
 * @par Purpose:
 *     Owns the GL handles for a vertex shader, fragment shader and linked
 *     program object loaded from pre-compiled .gxp binaries.  All
 *     Vita-specific GL shader objects should be managed through this class.
 */
/******************************************************************************/
#ifndef VITA_SHADER_PROGRAM_H
#define VITA_SHADER_PROGRAM_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include <stdint.h>

/**
 * RAII owner of a vitaGL pre-compiled Cg shader program.
 *
 * Usage:
 *   VitaShaderProgram prog;
 *   prog.Load(GL_VERTEX_SHADER,   "app0:shaders/foo_v.gxp");
 *   prog.Load(GL_FRAGMENT_SHADER, "app0:shaders/foo_f.gxp");
 *   prog.BindAttrib(0, "aPos");
 *   prog.Link("foo");
 *   prog.Bind();
 *   GLint loc = prog.GetUniform("myUniform");
 *   ...
 *   prog.Free();
 */
class VitaShaderProgram {
public:
    VitaShaderProgram() = default;
    ~VitaShaderProgram() { Free(); }

    // Non-copyable
    VitaShaderProgram(const VitaShaderProgram&) = delete;
    VitaShaderProgram& operator=(const VitaShaderProgram&) = delete;

    /**
     * Load and compile a single shader stage from a pre-compiled .gxp binary.
     * Type must be GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
     * Returns true on success.
     */
    bool Load(GLenum type, const char* gxp_path);

    /** Bind an attribute location before linking. */
    void BindAttrib(GLuint index, const char* name);

    /**
     * Create and link the program from previously loaded shaders.
     * @param debug_name  Shown in error messages.
     * Returns true on success.
     */
    bool Link(const char* debug_name);

    /** Activate this program for subsequent draw calls. */
    void Bind() const;

    /** Retrieve a uniform location (returns -1 if not found). */
    GLint GetUniform(const char* name) const;

    /** Release all GL resources. Safe to call on a not-yet-loaded object. */
    void Free();

    bool IsLinked() const { return m_program != 0; }

private:
    GLuint m_vert    = 0;
    GLuint m_frag    = 0;
    GLuint m_program = 0;
};

#endif // PLATFORM_VITA
#endif // VITA_SHADER_PROGRAM_H
