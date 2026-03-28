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
#include <vector>
#include "renderer/IWorldViewRenderer.h"
#include "renderer/WorldVertex.h"
#include "bflib_render.h"   // PolyPoint (needed by ShadowCmd)

class ITileAtlas;
class SoftwareWorldViewRenderer;
class GLTextRenderer;

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
    void BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h,
                        int vp_x, int vp_y) override;
    void FlushIsometricView() override;
    void FlushFrontView(struct Camera* cam) override;
    const char* GetName() const override { return "GLWorldViewRenderer"; }

    // Called by RendererOpenGL::EndFrame() to issue the accumulated draw call
    // after glClear() and before the CPU framebuffer blit overlay.
    // staging_buf must be the same buffer returned by LockFramebuffer(); it is
    // used to restore lbDisplay.WScreen for any sprite CPU-path fallbacks that
    // occur during depth-correct sprite replay.
    void GPUFlushNow(unsigned char* staging_buf = nullptr);

    // Render one keeper sprite quad GPU-side (called by the C hook kspr_hook_cb).
    // Public because kspr_hook_cb is a C-linkage static function.
    int render_keepersprite_gpu(long dst_x, long dst_y, long dst_w, long dst_h,
                                const unsigned char* data, int src_w, int src_h,
                                unsigned int draw_flags, const unsigned char* remap);

    // Add world-space text to be rendered at the given 3D position.
    // Text will be depth-tested and positioned correctly in the world view.
    // @param world_x, world_y, world_z 3D world position
    // @param text Text string to render  
    // @param color Text color index
    // @param scale Text scale factor (1.0 = normal size)
    // @param bucket_num Depth bucket for sorting (should match surrounding geometry)
    void AddWorldText(float world_x, float world_y, float world_z,
                     const char* text, int color, float scale, int bucket_num);

private:
    bool init_gl_resources();
    void free_gl_resources();
    bool compile_world_shaders();
    bool init_shadow_shader();
    bool init_keeper_sprite_shader();

    // Append one triangle (3 PolyPoint vertices, integer screen pixels) to the staging array.
    // tile_id is the flat block_ptrs[] index from p->block;
    // variation = tile_id / TEXTURE_BLOCKS_COUNT, tile_local = tile_id % TEXTURE_BLOCKS_COUNT.
    bool append_triangle(int tile_id,
                         const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2);

    // Append one triangle from compact-format fields (unsigned short xy, unsigned char uv/shade)
    bool append_triangle_compact(int sx0, int sy0, int u0, int v0, int shade0,
                                 int sx1, int sy1, int u1, int v1, int shade1,
                                 int sx2, int sy2, int u2, int v2, int shade2);

    // Record the current tile batch as a deferred draw command; advances the
    // batch start pointer.  No GL calls are issued — everything is replayed
    // in GPUFlushNow() after glClear().
    void gpu_flush();

    // Deferred draw command (built during FlushIsometricView, executed by GPUFlushNow)
    struct DrawCmd {
        enum Type { CMD_TILES, CMD_SPRITES, CMD_SHADOWS, CMD_WORLDTEXT } type;
        // CMD_TILES fields
        int vert_start  = 0;
        int vert_count  = 0;
        int variation   = 0;
        // CMD_SPRITES field
        int bucket_num  = 0;
        // CMD_SHADOWS field (index into m_shadow_cmds)
        int shadow_idx  = 0;
        // CMD_WORLDTEXT field (index into m_worldtext_cmds)
        int worldtext_idx = 0;
    };

    // Per-shadow data recorded during FlushIsometricView, consumed by GPUFlushNow
    struct ShadowCmd {
        struct PolyPoint verts[4];   // vertex_first..fourth (screen-px coords + 16.16 UV)
        unsigned short   anim_sprite;
        unsigned char    current_frame;
        short            angle;
        float            darkness;   // 1.0 - (color_value / 32.0); used as src_alpha for multiply-blend
    };

    // Per-world-text data recorded during FlushIsometricView, consumed by GPUFlushNow
    struct WorldTextCmd {
        float world_x, world_y, world_z; // 3D world position 
        float ndc_z;                     // Computed NDC depth for depth testing
        const char* text;                // Text string to render (must remain valid until GPUFlushNow)
        int   bucket_num;                // Bucket number for depth sorting
        int   color;                     // Text color index
        float scale;                     // Text scale factor
    };

    // Injected resources (not owned)
    ITileAtlas* m_atlas       = nullptr;
    GLuint      m_fade_tex    = 0;
    GLuint      m_palette_tex = 0;

    // GL objects owned by this renderer
    GLuint m_vao    = 0;
    GLuint m_vbo    = 0;
    GLuint m_shader = 0;

    // Shadow GL objects
    GLuint m_shadow_shader          = 0;
    GLuint m_shadow_silhouette_tex  = 0;
    GLuint m_shadow_vao             = 0;
    GLuint m_shadow_vbo             = 0;

    // Keeper-sprite GL objects (palette-indexed per-sprite texture + shader)
    GLuint m_kspr_shader        = 0;
    GLuint m_kspr_glow_shader   = 0;  // Additive glow variant — no palette, computes RGB delta directly
    GLuint m_kspr_sprite_tex    = 0;  // 256x256 GL_R8  — overwritten per sprite
    GLuint m_kspr_palette_tex   = 0;  // 256x1  GL_RGBA8 — palette LUT
    GLuint m_kspr_vao           = 0;
    GLuint m_kspr_vbo           = 0;

    // Uniform locations (cached at shader compile time)
    GLint  m_loc_tile_atlas  = -1;

    // Shadow uniform locations
    GLint  m_shadow_loc_viewport   = -1;
    GLint  m_shadow_loc_darkness   = -1;
    GLint  m_shadow_loc_silhouette = -1;

    // Keeper-sprite uniform locations
    GLint  m_kspr_loc_viewport = -1;
    GLint  m_kspr_loc_sprite   = -1;
    GLint  m_kspr_loc_palette  = -1;
    GLint  m_kspr_loc_alpha    = -1;
    GLint  m_kspr_loc_z_ndc    = -1;

    // Glow-shader uniform locations (shared vert; u_sprite + u_viewport + u_z_ndc only)
    GLint  m_kspr_glow_loc_viewport = -1;
    GLint  m_kspr_glow_loc_sprite   = -1;
    GLint  m_kspr_glow_loc_z_ndc    = -1;

    bool   m_kspr_palette_dirty = true;

    // Per-frame state
    int            m_screen_w   = 0;
    int            m_screen_h   = 0;
    int            m_vp_x       = 0;  // viewport left edge in screen pixels
    int            m_vp_y       = 0;  // viewport top edge in screen pixels
    unsigned char* m_framebuf   = nullptr; // viewport start in staging buffer
    int            m_pitch      = 0;       // staging buffer row stride (bytes)
    int            m_current_bucket = 0;   // bucket index being processed (used for depth z)

    // CPU-side vertex staging buffer (dynamic VBO)
    static const int k_max_verts = 65536;   // ~21000 triangles per frame
    WorldVertex* m_verts      = nullptr;
    int          m_vert_count = 0;
    int          m_cmd_vert_start = 0;  // start index of current accumulating tile batch
    int          m_current_variation = 0; // atlas variation currently staging

    // Deferred draw list — built during FlushIsometricView(), executed in GPUFlushNow()
    std::vector<DrawCmd>      m_draw_cmds;
    std::vector<ShadowCmd>    m_shadow_cmds;
    std::vector<WorldTextCmd> m_worldtext_cmds;

    // Software fallback for unported QKinds
    SoftwareWorldViewRenderer* m_sw_fallback = nullptr;
    bool m_sw_pass_active = false;

    // GPU text renderer for world-space floating text
    GLTextRenderer* m_text_renderer = nullptr;

    bool m_initialized = false;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_WORLD_VIEW_RENDERER_H
