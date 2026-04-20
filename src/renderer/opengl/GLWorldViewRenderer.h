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
#include <unordered_map>
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
    // Returns true only when the world renderer is initialised AND a world pass
    // (BeginWorldPass) was issued this frame — resets to false in GPUFlushNow.
    // The main menu never calls BeginWorldPass, so this returns false there,
    // allowing RendererOpenGL::EndFrame to run the CPU staging blit.
    bool IsGpuAccelerated() const override { return m_initialized && m_world_pass_active; }

    // Called by RendererOpenGL::EndFrame() to issue the accumulated draw call
    // after glClear() and before the CPU framebuffer blit overlay.
    void GPUFlushNow();

    /** Render the accumulated draw list into the currently-bound FBO.
     *  The caller is responsible for binding/unbinding the FBO and clearing it.
     *  m_screen_w/h must already be set to pip_w/pip_h via a preceding
     *  BeginWorldPass(nullptr, 0, pip_w, pip_h, 0, 0) call. */
    void GPUFlushNow_ToFBO(int pip_w, int pip_h);

    // IWorldViewRenderer: submit a keeper-sprite through the GPU path.
    int SubmitKeeperSprite(long dst_x, long dst_y, long dst_w, long dst_h,
                           const unsigned char* data, int src_w, int src_h,
                           unsigned int draw_flags, const unsigned char* remap) override;

    // Internal implementation used by SubmitKeeperSprite.
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

    // Called by GLUIRenderer::Flush() (via UIRenderer_Flush) to render power-hand
    // keeper sprites after glClear() with full-screen NDC coordinates.
    // Saves m_screen_w/h and m_current_sprite_z, sets them to MyScreenWidth/Height
    // and z=-1 (near plane, always on top), restores in EndHandSpriteRendering().
    void BeginHandSpriteRendering();
    void EndHandSpriteRendering();

    /** Clear the per-sprite decode atlas.  Must be called before new sprite
     *  data is loaded (e.g. between levels) so stale data-pointer → layer
     *  mappings are not reused. */
    void ClearKeeperSpriteAtlas();

    /** Attempt to initialise GL resources outside of a world pass, e.g. from
     *  RendererOpenGL::BeginFrame().  No-op when already initialised.
     *  Returns true when initialisation is complete. */
    bool TryEarlyInit() { return init_gl_resources(); }

private:
    bool init_gl_resources();
    void free_gl_resources();
    bool compile_world_shaders();
    bool init_shadow_shader();
    bool init_keeper_sprite_shader();
    bool init_flatpoly_shader();

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

    /** Core GL draw pass shared by GPUFlushNow() and GPUFlushNow_ToFBO().
     *  Uploads the vertex buffer, sets the given viewport (already in GL
     *  bottom-origin coords), executes all three draw passes, then resets the
     *  viewport to the full screen and clears the draw-command lists. */
    void gpu_execute_passes(int vp_x, int vp_y_gl);

    // Setup world sprite processing for a bucket (replaces global hook approach)
    void setup_world_sprite_processing(long bucket_num);

    // Deferred draw command (built during FlushIsometricView, executed by GPUFlushNow)
    struct DrawCmd {
        enum Type { CMD_TILES, CMD_SPRITES, CMD_SHADOWS, CMD_WORLDTEXT, CMD_FLAT_POLYS } type;
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

    // Per-shadow data recorded during FlushIsometricView, consumed by GPUFlushNow.
    // Sprite data is resolved eagerly during bucket walk so GPUFlushNow
    // stays pure-GPU (no calls back into engine_render C functions).
    struct ShadowCmd {
        struct PolyPoint verts[4];      // vertex_first..fourth (screen-px coords + 16.16 UV)
        const unsigned char* sprite_data; // resolved RLE sprite data (NULL = skip)
        int              sprite_w;      // decoded sprite width
        int              sprite_h;      // decoded sprite height
        bool             x_flip;        // true when silhouette needs horizontal flip
        float            darkness;      // 1.0 - (color_value / 32.0); used as src_alpha for multiply-blend
        float            ndc_z;          // NDC depth of shadow's floor bucket, used for depth testing
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

    // Saved viewport state during hand sprite rendering (see BeginHandSpriteRendering)
    int   m_saved_screen_w   = 0;
    int   m_saved_screen_h   = 0;
    float m_saved_sprite_z   = 0.0f;

    // Flat-colour polygon vertex: screen-pixel XY, NDC Z depth, linear RGB.
    // Built during FlushIsometricView for QK_PolyMode0/4/BasicPolygon,
    // uploaded once in GPUFlushNow and drawn with the flat-poly shader.
    struct FlatPolyVertex { float x, y, z, r, g, b; };

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
    GLuint m_kspr_sprite_tex    = 0;  // 256x256 GL_R8  — overwritten per sprite (fallback path)
    GLuint m_kspr_palette_tex   = 0;  // 256x1  GL_RGBA8 — palette LUT
    GLuint m_kspr_vao           = 0;
    GLuint m_kspr_vbo           = 0;

    // Keeper-sprite decode atlas: GL_TEXTURE_2D_ARRAY where each layer holds
    // one pre-decoded sprite (populated on first use, persists across frames).
    // Fallback to m_kspr_sprite_tex when atlas is full or unsupported.
    static const int k_kspr_atlas_layers = 512;
    GLuint m_kspr_sprite_array  = 0;  // GL_TEXTURE_2D_ARRAY 256×256×k_kspr_atlas_layers GL_R8
    GLuint m_kspr_atlas_shader  = 0;  // separate program using sampler2DArray
    int    m_kspr_atlas_used    = 0;  // next free layer index
    int    m_kspr_atlas_hits    = 0;  // cache hits this frame
    int    m_kspr_atlas_misses  = 0;  // cache misses (decode+upload) this frame
    struct AtlasEntry { int layer; int src_w; };
    std::unordered_map<const uint8_t*, AtlasEntry> m_kspr_atlas_map;

    // Flat-colour polygon GL objects (QK_PolyMode0/4/BasicPolygon — full GPU path)
    GLuint m_flatpoly_shader        = 0;
    GLuint m_flatpoly_vao           = 0;
    GLuint m_flatpoly_vbo           = 0;
    GLint  m_flatpoly_loc_viewport  = -1;

    // Uniform locations (cached at shader compile time)
    GLint  m_loc_tile_atlas  = -1;
    GLint  m_loc_palette     = -1;   // sampler1D u_palette (unit 1)
    // Shade / lighting uniforms (world fragment shader)
    GLint  m_loc_fullbright    = -1;  // u_fullbright
    GLint  m_loc_ambient       = -1;  // u_ambient
    GLint  m_loc_shade_scale   = -1;  // u_shade_scale
    GLint  m_loc_shade_gamma   = -1;  // u_shade_gamma
    GLint  m_loc_lighting_mode = -1;  // u_lighting_mode (0=software-accurate, 1=modern)
    GLint  m_loc_lightmap      = -1;  // usampler2D u_lightmap (unit 2)
    GLint  m_loc_tile_filter   = -1;  // u_tile_filter (0=nearest, 1=palette-correct bilinear)
    GLint  m_loc_missing_tile  = -1;  // u_missing_tile — diagnostic checkerboard when atlas absent
    int    m_tile_filter_applied = -1; // last GL filter mode applied; -1 = force re-apply on next flush
    // Lightmap texture (unit 2): mirrors game.lish.subtile_lightness[] as GL_R16UI
    GLuint m_tex_lightmap      = 0;

    // Shadow uniform locations
    GLint  m_shadow_loc_viewport   = -1;
    GLint  m_shadow_loc_darkness   = -1;
    GLint  m_shadow_loc_silhouette = -1;
    GLint  m_shadow_loc_ndc_z      = -1;

    // Keeper-sprite uniform locations
    GLint  m_kspr_loc_viewport = -1;
    GLint  m_kspr_loc_sprite   = -1;
    GLint  m_kspr_loc_palette  = -1;
    GLint  m_kspr_loc_alpha    = -1;
    GLint  m_kspr_loc_z_ndc    = -1;

    // Atlas-shader uniform locations (sampler2DArray variant)
    GLint  m_kspr_atlas_loc_viewport = -1;
    GLint  m_kspr_atlas_loc_sprite   = -1;
    GLint  m_kspr_atlas_loc_palette  = -1;
    GLint  m_kspr_atlas_loc_alpha    = -1;
    GLint  m_kspr_atlas_loc_z_ndc    = -1;
    GLint  m_kspr_atlas_loc_layer    = -1;

    // Glow-shader uniform locations (shared vert; u_sprite + u_viewport + u_z_ndc only)
    GLint  m_kspr_glow_loc_viewport = -1;
    GLint  m_kspr_glow_loc_sprite   = -1;
    GLint  m_kspr_glow_loc_z_ndc    = -1;

    // Depth-fail outline shaders and their uniform locations.
    // Single-texture variant (fallback/remapped sprites):
    GLuint m_kspr_outline_shader            = 0;
    GLint  m_kspr_outline_loc_viewport      = -1;
    GLint  m_kspr_outline_loc_sprite        = -1;
    GLint  m_kspr_outline_loc_z_ndc         = -1;
    GLint  m_kspr_outline_loc_color         = -1;
    // Array-atlas variant (normal sprites using GL_TEXTURE_2D_ARRAY):
    GLuint m_kspr_atlas_outline_shader           = 0;
    GLint  m_kspr_atlas_outline_loc_viewport     = -1;
    GLint  m_kspr_atlas_outline_loc_sprite       = -1;
    GLint  m_kspr_atlas_outline_loc_z_ndc        = -1;
    GLint  m_kspr_atlas_outline_loc_color        = -1;
    GLint  m_kspr_atlas_outline_loc_layer        = -1;

    bool   m_kspr_palette_dirty = true;

    // Per-frame state
    int            m_screen_w   = 0;
    int            m_screen_h   = 0;
    int            m_vp_x       = 0;  // viewport left edge in screen pixels
    int            m_vp_y       = 0;  // viewport top edge in screen pixels
    unsigned char* m_framebuf   = nullptr; // viewport start in staging buffer
    int            m_pitch      = 0;       // staging buffer row stride (bytes)
    int            m_current_bucket = 0;   // bucket index being processed (used for depth z)
    float          m_current_sprite_z = 0.0f; // NDC depth for current bucket sprites

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
    std::vector<FlatPolyVertex> m_flatpoly_verts;  // flat-colour polygon vertices, GPU-only

    // Software fallback for unported QKinds
    SoftwareWorldViewRenderer* m_sw_fallback = nullptr;
    bool m_sw_pass_active = false;

    // GPU text renderer for world-space floating text
    GLTextRenderer* m_text_renderer = nullptr;

    bool m_initialized = false;
    // Set to true in BeginWorldPass(); reset to false at the end of GPUFlushNow().
    // Tracks whether the world renderer is actually being used this frame.
    bool m_world_pass_active = false;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_WORLD_VIEW_RENDERER_H
