/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLWorldViewRenderer.h
 *     Desktop OpenGL world-geometry renderer.
 * @par Purpose:
 *     Implements IWorldViewRenderer for the desktop OpenGL path.
 *     FlushIsometricView() walks the engine bucket list, converts each
 *     PolyPoint triangle to WorldVertex (float NDC + UV + shade), batches
 *     all geometry into a single VBO, and issues one glDrawArrays call per
 *     frame.
 *
 *     The fragment shader (world_frag.glsl) replaces the entire CPU inner
 *     loop: palette-index lookup + fade-table lighting = two texture samples.
 *
 *     Unrecognised bucket kinds fall back to SoftwareWorldViewRenderer for
 *     that primitive, maintaining visual correctness while not all QKinds
 *     are ported.
 */
/******************************************************************************/
#ifndef GL_WORLD_VIEW_RENDERER_H
#define GL_WORLD_VIEW_RENDERER_H

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include "renderer/IWorldViewRenderer.h"
#include "renderer/WorldVertex.h"

class ITileAtlas;
class SoftwareWorldViewRenderer;

/******************************************************************************/

class GLWorldViewRenderer : public IWorldViewRenderer {
public:
    /**
     * @param atlas       Tile atlas providing GL texture handles.
     *                    Owned externally (RendererOpenGL); must outlive this.
     * @param fade_tex    GL texture handle for the 256×256 fade/lighting LUT.
     * @param palette_tex GL texture handle for the 256×1 RGBA palette.
     */
    GLWorldViewRenderer(ITileAtlas* atlas,
                        GLuint      fade_tex,
                        GLuint      palette_tex);
    ~GLWorldViewRenderer() override;

    // IWorldViewRenderer
    void BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h) override;
    void FlushIsometricView() override;
    void FlushFrontView(struct Camera* cam) override;
    const char* GetName() const override { return "GLWorldViewRenderer"; }

    // Called by RendererOpenGL::EndFrame() to issue the accumulated draw call
    // after glClear() and before the CPU framebuffer blit overlay.
    void GPUFlushNow();

private:
    bool init_gl_resources();
    void free_gl_resources();
    bool compile_world_shaders();

    // Append one triangle (3 PolyPoint vertices, fixed-point 16:16) to the staging array
    bool append_triangle(const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2);

    // Append one triangle from compact-format fields (unsigned short xy, unsigned char uv/shade)
    bool append_triangle_compact(int sx0, int sy0, int u0, int v0, int shade0,
                                 int sx1, int sy1, int u1, int v1, int shade1,
                                 int sx2, int sy2, int u2, int v2, int shade2);

    // Upload staged vertices and issue glDrawArrays; resets m_vert_count
    void gpu_flush();

    // Injected resources (not owned)
    ITileAtlas* m_atlas       = nullptr;
    GLuint      m_fade_tex    = 0;
    GLuint      m_palette_tex = 0;

    // GL objects owned by this renderer
    GLuint m_vao    = 0;
    GLuint m_vbo    = 0;
    GLuint m_shader = 0;

    // Uniform locations (cached at shader compile time)
    GLint  m_loc_tile_atlas  = -1;
    GLint  m_loc_fade_table  = -1;
    GLint  m_loc_palette     = -1;

    // Per-frame state
    int            m_screen_w   = 0;
    int            m_screen_h   = 0;
    unsigned char* m_framebuf   = nullptr; // viewport start in staging buffer
    int            m_pitch      = 0;       // staging buffer row stride (bytes)

    // CPU-side vertex staging buffer (dynamic VBO)
    static const int k_max_verts = 65536;   // ~21000 triangles per frame
    WorldVertex* m_verts     = nullptr;
    int          m_vert_count = 0;

    // Software fallback for unported QKinds
    SoftwareWorldViewRenderer* m_sw_fallback = nullptr;
    bool m_sw_pass_active = false;

    bool m_initialized = false;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_WORLD_VIEW_RENDERER_H
