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
 */
/******************************************************************************/
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include <atomic>
#include <vector>
#include <unordered_map>
#include "renderer/IWorldViewRenderer.h"
#include "renderer/WorldVertex.h"
#include "renderer/opengl/IGLShaderCompilable.h"
#include "renderer/ir/WorldCommands.h"
#include "renderer/RendererThread.h"
#include "bflib_render.h"   // PolyPoint (needed by ShadowCmd)

class ITileAtlas;

/******************************************************************************/

class GLWorldViewRenderer : public IWorldViewRenderer, public IGLShaderCompilable {
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

    /** Update the fade table texture handle (set after game tables are loaded). */
    void SetFadeTexture(GLuint tex) { m_fade_tex = tex; }

    // IWorldViewRenderer
    void BeginWorldPass(unsigned char* framebuf, int pitch, int w, int h,
                        int vp_x, int vp_y) override;
    void DrawIsometricView() override;
    void DrawFrontView(struct Camera* cam) override;
    const char* GetName() const override { return "GLWorldViewRenderer"; }
    const char* RendererName() const override { return "GLWorldViewRenderer"; }

    /** Compile all GLSL programs owned by this renderer and its internal
     *  text sub-renderer.  Idempotent — safe to call after construction
     *  (the first call also performs non-shader GL initialisation).
     *  Called by the bootstrapper in RendererManager::RendererInit(). */
    bool CompileShaders() override;
    /** Notify of the full OS-window dimensions (not the world viewport).
     *  Called by RendererOpenGL::BeginFrame() so that BeginHandSpriteRendering()
     *  and gpu_execute_passes() do not need to read MyScreenWidth/Height directly.
     *  Overrides IWorldViewRenderer::SetScreenSize(). */
    void SetScreenSize(int w, int h) override { m_full_screen_w = w; m_full_screen_h = h; }

    // Called by RendererOpenGL::EndFrame() to issue the accumulated draw list
    // after glClear() and before the CPU framebuffer blit overlay.
    void GPURenderNow();

    /** Redirect all geometry writes to dedicated PiP-only buffers so that
     *  the concurrent game thread's m_verts / m_draw_cmds are never touched.
     *  Must be called on the render thread before BeginWorldPass() / draw_view()
     *  for each PiP view.  GPURenderToFBO() ends the capture internally. */
    void BeginPiPCapture();

    /** Render the PiP geometry that was recorded after BeginPiPCapture() into
     *  the currently-bound FBO.  Ends the PiP capture and moves the pip
     *  command/vertex buffers into the rt_* slots for execution.
     *  The caller is responsible for binding/unbinding the FBO and clearing it.
     *  m_screen_w/h must already be set to pip_w/pip_h via a preceding
     *  BeginWorldPass(nullptr, 0, pip_w, pip_h, 0, 0) call. */
    void GPURenderToFBO(int pip_w, int pip_h);

    /** Swap game-thread ↔ render-thread command buffers.
     *  Must be called from RendererOpenGL::EndFrame() while the render thread
     *  is idle (i.e. before signalling it with m_rt_work_ready = true).
     *  Moves game-side data into the m_rt_* copies so the render thread reads
     *  a stable snapshot even after the game starts the next frame. */
    void FlipBuffers();

    bool HasPendingCommands() const
    {
        return !m_draw_cmds.empty() || !m_shadow_cmds.empty() || !m_flatpoly_verts.empty()
            || (m_vert_count > m_cmd_vert_start);
    }

    // ── IR (Intermediate Representation) path ─────────────────────────────────

    /** Set the IR write target for this frame.
     *  When @p cmds is non-null, SetWorldCommandBuffers activates the IR path;
     *  actual bucket-walk recording still writes to internal m_draw_cmds etc.
     *  (the internal double-buffer IS the world IR).  The pointer is used as a
     *  sentinel so ExecuteWorldFromIR() can be dispatched by the render thread.
     *  Call with nullptr to close the write window (e.g. during PiP). */
    void SetWorldCommandBuffers(WorldCommandBuffers* cmds) { m_world_write_cmds = cmds; }

    /** Replay the captured world geometry on the render thread.
     *  The world renderer already double-buffers its command list internally
     *  (FlipBuffers → m_rt_draw_cmds); this method simply calls GPURenderNow()
     *  so the render graph can dispatch through the IR interface.
     *  @param cmds  Read-side WorldCommandBuffers — not used directly yet (the
     *               data lives in m_rt_draw_cmds); accepted for interface symmetry
     *               with ExecuteUIFromIR / ExecuteTextFromIR. */
    void ExecuteWorldFromIR(const WorldCommandBuffers& cmds);

    /** IWorldViewRenderer override: calls ExecuteWorldFromIR().
     *  Note: EndFrame_GL() calls ExecuteWorldFromIR() directly (not through the
     *  interface) because it must be wrapped inside the lens-FBO bracket.  This
     *  override exists for interface completeness and future backends. */
    void ExecuteFromIR(const WorldCommandBuffers& cmds) override { ExecuteWorldFromIR(cmds); }

    /** Supply the active 256-colour VGA palette (768 bytes: R,G,B × 256).
     *  Called by RendererSetPaletteForRenderers() on the game thread.
     *  Pointer copy only — safe without GL context.
     *  Overrides IWorldViewRenderer::SetPaletteSource(). */
    void SetPaletteSource(const uint8_t* palette) override { m_palette_data = palette; }

    // IWorldViewRenderer: submit a keeper-sprite through the GPU path.
    int SubmitKeeperSprite(int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                           const unsigned char* data, int src_w, int src_h,
                           unsigned int draw_flags, const unsigned char* remap) override;

    // IWorldViewRenderer: clear per-level atlas cache.
    void ClearKeeperSpriteAtlas() override;

    // IWorldViewRenderer: preload all known sprites during level load so no
    // decode/upload ever occurs during gameplay.
    void PreloadKeeperSpriteAtlas() override;

   

    // Called by GLUIRenderer::Draw() (via UIRenderer_Draw) to render power-hand
    // keeper sprites after glClear() with full-screen NDC coordinates.
    // Saves the render-thread active viewport size and m_current_sprite_z, sets them
    // to full-screen and z=-1 (near plane, always on top), restores in EndHandSpriteRendering().
    void BeginHandSpriteRendering();
    void EndHandSpriteRendering();

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

    /** Render-thread: decode and GL-draw one keeper sprite.
     *  Called by gpu_execute_passes() for CMD_IR_KEEPER_SPRITES. */
    int render_keepersprite_gpu(int32_t dst_x, int32_t dst_y, int32_t dst_w, int32_t dst_h,
                                const unsigned char* data, int src_w, int src_h,
                                unsigned int draw_flags, const unsigned char* remap,
                                float z_ndc, int sprite_owner, int sprite_wants_outline);

    // Append one triangle (3 PolyPoint vertices, integer screen pixels) to the staging array.
    // tile_id is the flat block_ptrs[] index from p->block;
    // variation = tile_id / TEXTURE_BLOCKS_COUNT, tile_local = tile_id % TEXTURE_BLOCKS_COUNT.
    // cam_z0/1/2: camera-space Z for each vertex (for perspective-correct interpolation).
    //             Pass 0 for unknown/no-correction (defaults to 1.0).
    bool append_triangle(int tile_id,
                         const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2,
                         int32_t cam_z0 = 0, int32_t cam_z1 = 0, int32_t cam_z2 = 0);

    // Append one triangle from compact-format fields (unsigned short xy, unsigned char uv/shade)
    bool append_triangle_compact(int sx0, int sy0, int u0, int v0, int shade0,
                                 int sx1, int sy1, int u1, int v1, int shade1,
                                 int sx2, int sy2, int u2, int v2, int shade2);

    // Append a front-view textured quad (2 triangles = 6 vertices) from a BucketKindTexturedQuad.
    // Converts the axis-aligned screen quad to WorldVertex format using the tile atlas.
    bool append_frontview_quad(const struct BucketKindTexturedQuad* txquad);

    // Record the current tile batch as a deferred draw command; advances the
    // batch start pointer.  No GL calls are issued — everything is replayed
    // in GPURenderNow() after glClear().
    void gpu_flush();

    /** Core GL draw pass shared by GPURenderNow() and GPURenderToFBO().
     *  Uploads the vertex buffer, sets the given viewport (already in GL
     *  bottom-origin coords), executes all three draw passes, then resets the
     *  viewport to the full screen and clears the draw-command lists.
     *  @p kspr_ir  The keeper-sprite IR buffer to use for CMD_IR_KEEPER_SPRITES. */
    void gpu_execute_passes(int vp_x, int vp_y_gl, int screen_w, int screen_h,
                            const std::vector<IRWorldKeeperSpriteCmd>& kspr_ir);
    void ensure_clut_valid();
    void execute_preload_atlas();  // render-thread: bulk decode+upload of all known sprites

    /** Render-thread: draw one keeper sprite from an IR command (GL calls). */
    void DrawKeeperSpriteGL(const IRWorldKeeperSpriteCmd& cmd);

    // Setup world sprite processing for a bucket (sets m_current_sprite_z — GT only)
    void setup_world_sprite_processing(int32_t bucket_num);

    // Deferred draw command (built during DrawIsometricView/DrawFrontView, executed by GPURenderNow)
    struct DrawCmd {
        enum Type { CMD_TILES,
                    CMD_IR_KEEPER_SPRITES,  // keeper sprites captured on game thread
                    CMD_SHADOWS,
                    CMD_FLAT_POLYS,
                    CMD_PRELOAD_KSPR_ATLAS  // one-shot: bulk decode+upload on render thread
                  } type;
        // CMD_TILES fields
        int vert_start      = 0;
        int vert_count      = 0;
        // CMD_IR_KEEPER_SPRITES fields (indices into m_rt_kspr_ir / m_pip_kspr_ir)
        int sprite_ir_start = 0;
        int sprite_ir_count = 0;
        // CMD_SHADOWS field (index into m_shadow_cmds)
        int shadow_idx      = 0;
    };

    // Per-shadow data recorded during DrawIsometricView, consumed by GPURenderNow.
    // Sprite data is resolved eagerly during bucket walk so GPURenderNow
    // stays pure-GPU (no calls back into engine_render C functions).
    struct ShadowCmd {
        struct PolyPoint verts[4];      // vertex_first..fourth (screen-px coords + 16.16 UV)
        unsigned short   anim_sprite;   // passed to draw_keepsprite_unscaled_in_buffer
        short            angle;         // sprite_angle (already computed in create_shadows)
        unsigned char    current_frame; // animation frame
        int              tex_w;         // frame width  (FrameWidth for Rotable==0, SWidth for Rotable==2)
        int              tex_h;         // frame height (FrameHeight for Rotable==0, SHeight for Rotable==2)
        float            darkness;      // 1.0 - dist_sq/32.0; src_alpha for multiply-blend
        float            ndc_z;         // NDC depth of shadow's floor bucket, used for depth testing
    };

    // Saved viewport state during hand sprite rendering (see BeginHandSpriteRendering)
    int   m_saved_screen_w   = 0;
    int   m_saved_screen_h   = 0;
    float m_saved_sprite_z   = 0.0f;

    // Flat-colour polygon vertex: screen-pixel XY, NDC Z depth, linear RGB.
    // Built during DrawIsometricView for QK_PolyMode0/4/BasicPolygon,
    // uploaded once in GPURenderNow and drawn with the flat-poly shader.
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
    GLuint m_kspr_vao           = 0;
    GLuint m_kspr_vbo           = 0;

    // Keeper-sprite decode atlas: GL_TEXTURE_2D_ARRAY where each layer holds
    // one pre-decoded sprite (populated on first use, persists across frames).
    // Fallback to m_kspr_sprite_tex when atlas is full or unsupported.
    static const int k_kspr_atlas_layers = 512;
    GLuint m_kspr_sprite_array  = 0;  // GL_TEXTURE_2D_ARRAY 256×256×k_kspr_atlas_layers GL_R8
    GLuint m_kspr_atlas_shader  = 0;  // separate program using sampler2DArray
    int    m_kspr_atlas_used    = 0;  // next free layer index
    int    m_kspr_atlas_peak    = 0;  // high-water mark across the session (for sizing k_kspr_atlas_layers)
    int    m_kspr_atlas_hits    = 0;  // cache hits this frame
    int    m_kspr_atlas_misses  = 0;  // cache misses (decode+upload) this frame
    struct AtlasEntry { int layer; int src_w; };
    std::unordered_map<const uint8_t*, AtlasEntry> m_kspr_atlas_map;

    // CLUT (Colour Lookup Table): 256×k_clut_rows GL_RGBA8 texture.
    // Row 0 = identity (palette[i] for all i).
    // Rows 1..k_clut_rows-1 = per-remap CLUTs (palette[remap[i]]).
    static const int k_clut_rows = 128;
    GLuint m_kspr_clut_tex      = 0;
    int    m_kspr_clut_used     = 1;   // next free row (0 = identity, always allocated)
    std::unordered_map<const uint8_t*, int> m_kspr_clut_map;
    uint8_t m_kspr_clut_palette_snap[768] = {};

    // Flat-colour polygon GL objects (QK_PolyMode0/4/BasicPolygon — full GPU path)
    GLuint m_flatpoly_shader        = 0;
    GLuint m_flatpoly_vao           = 0;
    GLuint m_flatpoly_vbo           = 0;
    GLint  m_flatpoly_loc_viewport  = -1;

    // Uniform locations (cached at shader compile time)
    GLint  m_loc_tile_atlas  = -1;
    GLint  m_loc_palette     = -1;   // sampler2D u_palette (unit 1)
    // Shade / lighting uniforms (world fragment shader)
    GLint  m_loc_fullbright    = -1;  // u_fullbright
    GLint  m_loc_ambient       = -1;  // u_ambient
    GLint  m_loc_shade_scale   = -1;  // u_shade_scale
    GLint  m_loc_shade_gamma   = -1;  // u_shade_gamma
    GLint  m_loc_lighting_mode = -1;  // u_lighting_mode (0=software-accurate, 1=modern)
    GLint  m_loc_darkness_mode  = -1;  // u_darkness_mode (0=linear, 1=palette LUT, 2=fog)
    GLint  m_loc_fade_table     = -1;  // sampler2D u_fade_table (unit 3)
    GLint  m_loc_time           = -1;  // u_time (seconds, fog animation)
    GLint  m_loc_fog_speed      = -1;  // u_fog_speed
    GLint  m_loc_fog_density    = -1;  // u_fog_density
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
    GLint  m_kspr_atlas_loc_clut     = -1;  // u_clut (unit 1)
    GLint  m_kspr_atlas_loc_alpha    = -1;
    GLint  m_kspr_atlas_loc_z_ndc    = -1;
    GLint  m_kspr_atlas_loc_layer    = -1;
    GLint  m_kspr_atlas_loc_clut_v   = -1;  // u_clut_v (CLUT row V coord)

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

    // Atlas glow shader for additive sprites (sampler2DArray variant)
    GLuint m_kspr_atlas_glow_shader          = 0;
    GLint  m_kspr_atlas_glow_loc_viewport    = -1;
    GLint  m_kspr_atlas_glow_loc_sprite      = -1;
    GLint  m_kspr_atlas_glow_loc_z_ndc       = -1;
    GLint  m_kspr_atlas_glow_loc_layer       = -1;

    // Full OS-window dimensions — set by SetFullScreenSize(), used in
    // BeginHandSpriteRendering() and gpu_execute_passes() for full-screen viewport.
    int            m_full_screen_w = 0;  // GT: (SetScreenSize is game-thread-only)
    int            m_full_screen_h = 0;  // GT:

    // Active VGA palette (R,G,B × 256) — source pointer registered via SetPaletteSource by SetPaletteData(), eliminates lbPalette read.
    const uint8_t* m_palette_data  = nullptr;  // GT:

    // Per-frame state — game-thread write, snapshotted at FlipBuffers
    int            m_screen_w   = 0;   // GT:
    int            m_screen_h   = 0;   // GT:
    int            m_vp_x       = 0;   // GT: viewport left edge in screen pixels
    int            m_vp_y       = 0;   // GT: viewport top edge in screen pixels
    unsigned char* m_framebuf   = nullptr; // GT: viewport start in staging buffer
    int            m_pitch      = 0;       // GT: staging buffer row stride (bytes)
    int            m_current_bucket   = 0;   // GT: bucket index being processed
    float          m_current_sprite_z = 0.0f; // GT: NDC depth for current bucket's sprites
    int            m_draw_screen_w = 0;   // RT: active viewport width for GL draw calls
    int            m_draw_screen_h = 0;   // RT: active viewport height for GL draw calls

    // CPU-side vertex staging buffer (dynamic VBO)
    static const int k_max_verts = 65536;   // ~21000 triangles per frame
    WorldVertex* m_verts      = nullptr;    // GT: game-thread write buffer
    int          m_vert_count = 0;          // GT: game-thread vertex count
    int          m_cmd_vert_start = 0;      // GT: start index of current accumulating tile batch

    // GT: Deferred draw list — built during DrawIsometricView(), executed in GPURenderNow()
    std::vector<DrawCmd>             m_draw_cmds;
    std::vector<ShadowCmd>           m_shadow_cmds;
    std::vector<FlatPolyVertex>      m_flatpoly_verts;
    std::vector<IRWorldKeeperSpriteCmd> m_kspr_ir;  // keeper sprite capture (game thread)

    // ── Double-buffer render copies ───────────────────────────────────────────
    // FlipBuffers() (called from RendererOpenGL::EndFrame before signalling the
    // render thread) std::moves the game-thread vectors here and swaps the
    // raw vertex pointers, so the game thread can start the next frame while
    // the render thread reads from these stable render-side copies.
    WorldVertex* m_rt_verts       = nullptr;  // RT: render-thread read buffer
    int          m_rt_vert_count  = 0;        // RT:
    std::vector<DrawCmd>             m_rt_draw_cmds;       // RT:
    std::vector<ShadowCmd>           m_rt_shadow_cmds;     // RT:
    std::vector<FlatPolyVertex>      m_rt_flatpoly_verts;  // RT:
    std::vector<IRWorldKeeperSpriteCmd> m_rt_kspr_ir;      // RT: keeper sprites after FlipBuffers
    int          m_rt_screen_w = 0;   // RT:
    int          m_rt_screen_h = 0;   // RT:
    int          m_rt_vp_x     = 0;   // RT:
    int          m_rt_vp_y     = 0;   // RT:
    uint8_t      m_rt_palette[768] = {};  // RT:

    // ── PiP-only buffers (render-thread write, never touched by game thread) ─
    // BeginPiPCapture() switches all geometry writes here so draw_view() called
    // from the render thread (PiP) never races with the game thread's m_verts.
    WorldVertex* m_pip_verts           = nullptr;  // RT(PiP):
    int          m_pip_vert_count      = 0;         // RT(PiP):
    int          m_pip_cmd_vert_start  = 0;         // RT(PiP):
    std::vector<DrawCmd>             m_pip_draw_cmds;       // RT(PiP):
    std::vector<ShadowCmd>           m_pip_shadow_cmds;     // RT(PiP):
    std::vector<FlatPolyVertex>      m_pip_flatpoly_verts;  // RT(PiP):
    std::vector<IRWorldKeeperSpriteCmd> m_pip_kspr_ir;      // RT(PiP): keeper sprites during PiP
    bool         m_pip_capture         = false;             // RT:

    bool m_initialized = false;
    // Set to true in BeginWorldPass(); reset to false at the end of GPURenderNow().
    // Tracks whether the world renderer is actually being used this frame.
    bool m_world_pass_active = false;  // GT:

    // IR write target — set by SetWorldCommandBuffers(); used as sentinel.
    WorldCommandBuffers* m_world_write_cmds = nullptr;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
