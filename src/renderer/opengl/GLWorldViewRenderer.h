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
#include <array>
#include <vector>
#include <unordered_map>
#include "renderer/IWorldViewRenderer.h"
#include "renderer/WorldVertex.h"
#include "renderer/FlatPolyVertex.h"
#include "renderer/EngineVertex.h"
#include "renderer/opengl/IGLShaderCompilable.h"
#include "renderer/ir/WorldCommands.h"
#include "renderer/ir/IRCommandBuffer.h"
#include "renderer/RendererThread.h"

struct PolyPoint;
class ITileAtlas;

/******************************************************************************/

class GLWorldViewRenderer : public IWorldViewRenderer, public IGLShaderCompilable {
public:
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

    // Public struct for captured PiP world geometry (populated on game thread).
    struct PiPCapture {
        std::vector<DrawCmd>                draw_cmds;
        std::vector<WorldVertex>            tile_verts;
        std::vector<FlatPolyVertex>         flat_poly_verts;
        std::vector<IRWorldKeeperSpriteCmd> kspr_ir;
        std::vector<IRWorldShadowCmd>       shadow_cmds;
        int screen_w = 0;
        int screen_h = 0;
    };

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
    void GPURenderNow(const WorldCommandBuffers& cmds);

    /** Redirect all geometry writes to dedicated PiP-only buffers so that
     *  PiP draw_view() capture can reuse the legacy bucket walk without touching
     *  the main frame's world IR buffers. */
    void BeginPiPCapture();

    /** Finalize the current PiP capture on the game thread and return the
     *  snapshotted world geometry for render-thread execution. */
    PiPCapture FinalizePiPCapture();

    /** Execute a pre-captured PiP world snapshot into the currently-bound FBO. */
    void ExecutePiPCapture(const PiPCapture& cap, int pip_w, int pip_h);

    /** Swap game-thread ↔ render-thread command buffers.
     *  Must be called from RendererOpenGL::EndFrame() while the render thread
     *  is idle (i.e. before signalling it with m_rt_work_ready = true).
     *  Moves game-side data into the m_rt_* copies so the render thread reads
     *  a stable snapshot even after the game starts the next frame. */
    void FlipBuffers();

    bool HasPendingCommands() const
    {
        return !m_draw_cmds.empty() || !m_shadow_cmds.empty()
            || (m_world_write_cmds != nullptr
                && (!m_world_write_cmds->flat_poly_verts.empty() || !m_world_write_cmds->shadows.Empty()))
            || (m_vert_count > m_cmd_vert_start);
    }

    // ── IR (Intermediate Representation) path ─────────────────────────────────

    /** Set the IR write target for this frame.
     *  When @p cmds is non-null, SetWorldCommandBuffers activates the IR path;
     *  actual bucket-walk recording still writes ordering to internal m_draw_cmds
     *  while tile/flat-poly vertex data is written into @p cmds.
     *  Call with nullptr to close the write window (e.g. during PiP). */
    void SetWorldCommandBuffers(WorldCommandBuffers* cmds) override { m_world_write_cmds = cmds; }

    /** Replay the captured world geometry on the render thread.
     *  The world renderer still uses its internal ordered draw-command stream
     *  (FlipBuffers → m_rt_draw_cmds), but tile/flat-poly vertex data comes from
     *  the render-graph-owned WorldCommandBuffers passed here. */
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
                           unsigned int draw_flags, const unsigned char* remap,
                           int32_t sprite_id) override;
    int SubmitWorldShadowCmd(const IRWorldShadowCmd& cmd) override;

    // IWorldViewRenderer: clear per-level atlas cache.
    void ClearKeeperSpriteAtlas() override;

    // IWorldViewRenderer: preload all known sprites during level load so no
    // decode/upload ever occurs during gameplay.
    void PreloadKeeperSpriteAtlas() override;

   

    /** Game thread: redirect keeper-sprite submits to the cursor shadow buffer.
     *  Called by GLCursorLayer::SubmitKeeperHandSprite() before calling
     *  process_keeper_sprite_ex() so the resulting SubmitKeeperSprite() calls
     *  are captured to m_cursor_kspr_ir instead of m_kspr_ir.
     *  Must be followed by EndCursorCapture() on the same thread. */
    void BeginCursorCapture();
    void EndCursorCapture();

    /** Render thread: draw all pre-computed cursor keeper sprites from
     *  m_rt_cursor_kspr_ir.  Sets up full-screen viewport + blend state,
     *  calls render_keepersprite_gpu() for each sprite, then restores state.
     *  Called by GLCursorLayer::ExecuteCursorFromIR(). */
    void DrawCursorKeeperSprites();

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
                                float z_ndc, int sprite_owner, int sprite_wants_outline,
                                int32_t sprite_id);

    // Append one triangle (3 PolyPoint vertices, integer screen pixels) to the staging array.
    // tile_id is the flat block_ptrs[] index from p->block;
    // variation = tile_id / TEXTURE_BLOCKS_COUNT, tile_local = tile_id % TEXTURE_BLOCKS_COUNT.
    // cam_z0/1/2: camera-space Z for each vertex (for perspective-correct interpolation).
    //             Pass 0 for unknown/no-correction (defaults to 1.0).
    bool append_triangle(int tile_id,
                         const struct PolyPoint* p0,
                         const struct PolyPoint* p1,
                         const struct PolyPoint* p2,
                         int32_t cam_z0 = 0, int32_t cam_z1 = 0, int32_t cam_z2 = 0,
                         int32_t wx0 = 0, int32_t wy0 = 0, int32_t wz0 = 0,
                         int32_t wx1 = 0, int32_t wy1 = 0, int32_t wz1 = 0,
                         int32_t wx2 = 0, int32_t wy2 = 0, int32_t wz2 = 0);

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

    /** Core GL draw pass shared by GPURenderNow() and ExecutePiPCapture().
     *  Uploads the vertex buffer, sets the given viewport (already in GL
     *  bottom-origin coords), executes all three draw passes, then resets the
     *  viewport to the full screen and clears the draw-command lists.
     *  @p kspr_ir  The keeper-sprite IR buffer to use for CMD_IR_KEEPER_SPRITES. */
    void gpu_execute_passes(int vp_x, int vp_y_gl, int screen_w, int screen_h,
                            const std::vector<WorldVertex>& tile_verts,
                            const std::vector<FlatPolyVertex>& fp_verts,
                            const std::vector<IRWorldKeeperSpriteCmd>& kspr_ir,
                            const IRCommandBuffer<IRWorldShadowCmd>* ir_shadows = nullptr);
    void DrawShadowGL(const IRWorldShadowCmd& cmd, int screen_w, int screen_h);
    void ensure_clut_valid();
    void execute_preload_atlas();  // render-thread: bulk decode+upload of all known sprites

    /** Render-thread: draw one keeper sprite from an IR command (GL calls). */
    void DrawKeeperSpriteGL(const IRWorldKeeperSpriteCmd& cmd);

    // ── Instanced keeper-sprite fast path (render thread) ────────────────────
    // All atlas-resident sprites of a frame collapse into one (or two, with
    // outlines) glDrawArraysInstanced calls instead of a draw per sprite.
    // Sprites that cannot live in the atlas (atlas full) force a flush and
    // fall back to DrawKeeperSpriteGL so painter's order is preserved.

    /** Per-instance record for the main instanced sprite pass. */
    struct KsprInstance {
        float    rect[4];   // dst x, y, w, h (screen px)
        float    uvext[2];  // src_w/decode_dim, src_h/decode_dim
        float    layer;     // atlas layer
        float    clut_v;    // CLUT row V coord (row 0 = identity)
        float    alpha;     // 1.0 / transpar4 / transpar8
        float    z_ndc;     // pre-computed NDC depth
        uint32_t flags;     // bit0 = flip_h, bit1 = additive glow
    };
    /** Per-instance record for the depth-fail outline pass. */
    struct KsprOutlineInstance {
        float rect[4];
        float uvext[2];
        float layer;
        float z_ndc;        // sprite z + outline bias
        float flip;         // 0/1
        float color[4];     // owner colour + outline alpha
    };

    bool  init_keeper_sprite_instancing();
    /** Look up (or decode + upload) the atlas layer for a sprite.
     *  Keyed by @p sprite_id (stable across sprite-heap eviction); @p data is
     *  only read on a cache miss to decode.  Returns -1 when the atlas is
     *  absent or full, or when sprite_id is unknown (< 0). */
    int   resolve_atlas_layer(int32_t sprite_id, const unsigned char* data, int src_w, int src_h);
    /** Look up (or lazily build) the CLUT row for a remap table.
     *  Returns the row's V texcoord; identity row 0 when the CLUT is full. */
    float resolve_clut_v(const unsigned char* remap);
    void  append_keeper_sprite_instance(const IRWorldKeeperSpriteCmd& cmd);
    void  flush_keeper_sprite_instances();

    // ── Instanced shadow fast path (render thread) ───────────────────────────
    // Silhouette masks are decoded once per (sprite, frame, quarter, flip)
    // variant into a texture-array cache, then every shadow of the frame —
    // circle and silhouette alike — draws as one instanced call.

    /** Per-instance record for the instanced shadow pass (sheared quad). */
    struct ShadowInstance {
        float c01[4];   // corner0.xy, corner1.xy (screen px)
        float c23[4];   // corner2.xy, corner3.xy
        float uv01[4];  // uv0, uv1
        float uv23[4];  // uv2, uv3
        float ldc[4];   // layer, darkness, ndc_z, is_circle
    };

    bool init_shadow_instancing();
    /** Look up (or decode + upload) the silhouette-cache layer for a shadow.
     *  Returns -1 when the cache is absent/full or the variant is unsupported. */
    int  resolve_shadow_silhouette_layer(const IRWorldShadowCmd& sc);
    void append_shadow_instance(const IRWorldShadowCmd& sc, int screen_w, int screen_h);
    void flush_shadow_instances();

    // Setup world sprite processing for a bucket (sets m_current_sprite_z — GT only)
    void setup_world_sprite_processing(int32_t bucket_num);

    using ShadowCmd = IRWorldShadowCmd;

    // Saved viewport state during hand sprite rendering (see BeginHandSpriteRendering)
    int   m_saved_screen_w   = 0;
    int   m_saved_screen_h   = 0;
    float m_saved_sprite_z   = 0.0f;

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
    GLuint m_shadow_circle_tex      = 0;  // pre-baked 64×64 GL_R8 soft circle (generated at init)
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
    // Keyed by frame-resolved global sprite index, NOT the data pointer: the
    // sprite heap can evict and reuse block addresses mid-level, so a pointer
    // key could silently serve stale pixels for a recycled address.
    std::unordered_map<int32_t, AtlasEntry> m_kspr_atlas_map;

    // CLUT (Colour Lookup Table): 256×k_clut_rows GL_RGBA8 texture.
    // Row 0 = identity (palette[i] for all i).
    // Rows 1..k_clut_rows-1 = per-remap CLUTs (palette[remap[i]]).
    static const int k_clut_rows = 128;
    GLuint m_kspr_clut_tex      = 0;
    int    m_kspr_clut_used     = 1;   // next free row (0 = identity, always allocated)
    std::vector<std::array<uint8_t, 256>> m_kspr_clut_remaps;
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
    GLint  m_shadow_loc_colour     = -1;  // u_shadow_colour (vec4: rgb tint + intensity)

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

    // Instanced shadow GL objects (see ShadowInstance above).
    // The silhouette cache is a GL_TEXTURE_2D_ARRAY keyed by the variant
    // resolve_keepsprite_shadow_variant() reports — decoding runs once per
    // variant per level instead of once per shadow per frame.
    static const int k_shadow_sil_layers = 512;
    GLuint m_shadow_sil_array        = 0;
    int    m_shadow_sil_used         = 0;
    int    m_shadow_sil_peak         = 0;
    std::unordered_map<uint64_t, int> m_shadow_sil_map;  // variant key → layer
    GLuint m_shadow_inst_shader      = 0;
    GLuint m_shadow_inst_vao         = 0;
    GLuint m_shadow_inst_vbo         = 0;
    GLint  m_shadow_inst_loc_viewport = -1;
    GLint  m_shadow_inst_loc_colour   = -1;
    std::vector<ShadowInstance> m_shadow_instances;      // RT: batch scratch

    // Instanced keeper-sprite GL objects (see KsprInstance above).
    // m_kspr_inst_quad_vbo holds the static unit quad shared by both VAOs;
    // the instance VBOs are orphaned each flush (GL_STREAM_DRAW).
    GLuint m_kspr_inst_shader               = 0;
    GLuint m_kspr_inst_outline_shader       = 0;
    GLuint m_kspr_inst_quad_vbo             = 0;
    GLuint m_kspr_inst_vao                  = 0;
    GLuint m_kspr_inst_vbo                  = 0;
    GLuint m_kspr_inst_outline_vao          = 0;
    GLuint m_kspr_inst_outline_vbo          = 0;
    GLint  m_kspr_inst_loc_viewport         = -1;
    GLint  m_kspr_inst_outline_loc_viewport = -1;
    std::vector<KsprInstance>        m_kspr_instances;         // RT: batch scratch
    std::vector<KsprOutlineInstance> m_kspr_outline_instances; // RT: batch scratch

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
    int          m_vert_count = 0;          // GT: vertex count in WorldCommandBuffers::tile_verts
    int          m_cmd_vert_start = 0;      // GT: start index of current accumulating tile batch

    // GT: Deferred draw list — built during DrawIsometricView(), executed in GPURenderNow()
    std::vector<DrawCmd>             m_draw_cmds;
    std::vector<ShadowCmd>           m_shadow_cmds;
    std::vector<IRWorldKeeperSpriteCmd> m_kspr_ir;  // keeper sprite capture (game thread)

    // ── Double-buffer render copies ───────────────────────────────────────────
    // FlipBuffers() (called from RendererOpenGL::EndFrame before signalling the
    // render thread) std::moves the game-thread vectors here so the game thread
    // can start the next frame while the render thread reads stable copies.
    std::vector<DrawCmd>             m_rt_draw_cmds;       // RT:
    std::vector<ShadowCmd>           m_rt_shadow_cmds;     // RT:
    std::vector<IRWorldKeeperSpriteCmd> m_rt_kspr_ir;      // RT: keeper sprites after FlipBuffers
    int          m_rt_screen_w = 0;   // RT:
    int          m_rt_screen_h = 0;   // RT:
    int          m_rt_vp_x     = 0;   // RT:
    int          m_rt_vp_y     = 0;   // RT:
    uint8_t      m_rt_palette[768] = {};  // RT:

    // ── PiP-only buffers (game-thread capture, render-thread execution after snapshot) ─
    std::vector<WorldVertex>            m_pip_verts;          // GT(PiP capture):
    int                                 m_pip_vert_count      = 0; // GT(PiP capture):
    int                                 m_pip_cmd_vert_start  = 0; // GT(PiP capture):
    std::vector<DrawCmd>                m_pip_draw_cmds;      // GT(PiP capture):
    std::vector<ShadowCmd>              m_pip_shadow_cmds;    // GT(PiP capture):
    std::vector<FlatPolyVertex>         m_pip_flatpoly_verts; // GT(PiP capture):
    std::vector<IRWorldKeeperSpriteCmd> m_pip_kspr_ir;        // GT(PiP capture):
    bool                                m_pip_capture         = false; // GT:
    bool         m_cursor_capture      = false;             // GT: redirect SubmitKeeperSprite → m_cursor_kspr_ir
    bool         m_cursor_pass_active  = false;             // RT: force non-atlas keeper-sprite path for cursor/hand sprites

    // ── Cursor keeper-sprite double buffer ────────────────────────────────────
    // Game thread pre-computes cursor sprites (via process_keeper_sprite_ex) into
    // m_cursor_kspr_ir during SubmitKeeperHandSprite().  FlipBuffers() moves them
    // to m_rt_cursor_kspr_ir for the render thread to draw via DrawCursorKeeperSprites().
    std::vector<IRWorldKeeperSpriteCmd> m_cursor_kspr_ir;     // GT: write
    std::vector<IRWorldKeeperSpriteCmd> m_rt_cursor_kspr_ir;  // RT: read

    // ── Lightmap shadow copy ──────────────────────────────────────────────────
    // FlipBuffers() snapshots game.lish.subtile_lightness[] here so the render
    // thread never touches the live game array.  511×511×2 bytes ≈ 0.5 MB.
    static constexpr int k_lightmap_w = 511;
    static constexpr int k_lightmap_h = 511;
    uint16_t m_rt_lightmap[k_lightmap_w * k_lightmap_h] = {};

    bool m_initialized = false;
    // Set to true in BeginWorldPass(); reset to false at the end of GPURenderNow().
    // Tracks whether the world renderer is actually being used this frame.
    bool m_world_pass_active = false;  // GT:

    // IR write target — set by SetWorldCommandBuffers(); used as sentinel.
    WorldCommandBuffers* m_world_write_cmds = nullptr;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
