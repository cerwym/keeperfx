/******************************************************************************/
/** @file KfxProfiling.h
 *   Tracy + RenderDoc instrumentation helpers — C++ renderer files only.
 *
 *   Include this after pre_inc.h in any renderer .cpp file that wants zones.
 *   Compiles to zero overhead when neither TRACY_ENABLE nor RENDERER_OPENGL_ENABLED
 *   is defined, so it is safe to include unconditionally.
 */
/******************************************************************************/
#pragma once

// ── Tracy CPU zones ──────────────────────────────────────────────────────────
#ifdef TRACY_ENABLE
#  include <tracy/Tracy.hpp>
#  define KFX_ZONE(name)        ZoneScopedN(name)
#  define KFX_PLOT(name, val)   TracyPlot(name, static_cast<int64_t>(val))
#  define KFX_FRAMEMARK()       FrameMark
#else
#  define KFX_ZONE(name)
#  define KFX_PLOT(name, val)
#  define KFX_FRAMEMARK()
#endif

// glad must be included before TracyOpenGL.hpp since Tracy's GL header uses
// GL types and entry-points directly without pulling them in itself.
#ifdef RENDERER_OPENGL_ENABLED
#  include <glad/glad.h>
#  include <cstdio>
#endif

// ── Tracy GPU zones (OpenGL backend) ─────────────────────────────────────────
// Caller is responsible for TracyGpuContext (init) and TracyGpuCollect (per-frame).
#if defined(TRACY_ENABLE) && defined(RENDERER_OPENGL_ENABLED)
#  include <tracy/TracyOpenGL.hpp>
#  define KFX_GPU_ZONE(name)    TracyGpuZone(name)
#  define KFX_GPU_CTX_CREATE()  TracyGpuContext
#  define KFX_GPU_COLLECT()     TracyGpuCollect
#else
#  define KFX_GPU_ZONE(name)
#  define KFX_GPU_CTX_CREATE()
#  define KFX_GPU_COLLECT()
#endif

// ── RenderDoc GL debug labels & groups ───────────────────────────────────────
// glObjectLabel / glPushDebugGroup are GL_KHR_debug (core in 4.3, extension in
// 3.3).  All calls are guarded at runtime by GLAD_GL_KHR_debug.  When
// RENDERER_OPENGL_ENABLED is not defined the macros expand to nothing.
#ifdef RENDERER_OPENGL_ENABLED
// glad and cstdio already included above

namespace kfx_gl_debug {
    inline void push_group(const char* name) {
#ifdef GL_KHR_debug
        if (GLAD_GL_KHR_debug) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, name);
#endif
    }
    inline void pop_group() {
#ifdef GL_KHR_debug
        if (GLAD_GL_KHR_debug) glPopDebugGroup();
#endif
    }
    inline void label_obj(GLenum type, GLuint id, const char* name) {
#ifdef GL_KHR_debug
        if (GLAD_GL_KHR_debug) glObjectLabel(type, id, -1, name);
#endif
    }
    inline void label_obj_fmt(GLenum type, GLuint id, const char* fmt, int idx) {
#ifdef GL_KHR_debug
        if (GLAD_GL_KHR_debug) {
            char buf[64];
            snprintf(buf, sizeof(buf), fmt, idx);
            glObjectLabel(type, id, -1, buf);
        }
#endif
    }
} // namespace kfx_gl_debug

#  define KFX_GL_PUSH(name)                kfx_gl_debug::push_group(name)
#  define KFX_GL_POP()                     kfx_gl_debug::pop_group()
#  define KFX_GL_LABEL(type, id, name)     kfx_gl_debug::label_obj(type, id, name)
#  define KFX_GL_LABEL_FMT(type, id, f, n) kfx_gl_debug::label_obj_fmt(type, id, f, n)

/// RAII debug group scope — handles early-return paths correctly.
struct KfxGLDebugScope {
    explicit KfxGLDebugScope(const char* n) { kfx_gl_debug::push_group(n); }
    ~KfxGLDebugScope()                      { kfx_gl_debug::pop_group(); }
    KfxGLDebugScope(const KfxGLDebugScope&)            = delete;
    KfxGLDebugScope& operator=(const KfxGLDebugScope&) = delete;
};
/// Declare a scoped debug group — prefer over explicit push/pop when possible.
#  define KFX_GL_SCOPE(var, name) KfxGLDebugScope var(name)

#else  // !RENDERER_OPENGL_ENABLED

#  define KFX_GL_PUSH(name)
#  define KFX_GL_POP()
#  define KFX_GL_LABEL(type, id, name)
#  define KFX_GL_LABEL_FMT(type, id, f, n)
#  define KFX_GL_SCOPE(var, name)

#endif // RENDERER_OPENGL_ENABLED
