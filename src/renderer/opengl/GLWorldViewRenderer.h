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

private:
    bool init_gl_resources();
    void free_gl_resources();
    bool load_shader_source(const char* path, char** out_src);
    bool compile_world_shaders();

    // Append one triangle (3 PolyPoint vertices) to the VBO staging array
    // Returns false if the buffer is full (forces a mid-frame flush)
    bool append_triangle(const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2);

    // Flush whatever is in the staging array to the GPU and draw
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
    int   m_screen_w = 0;
    int   m_screen_h = 0;

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
