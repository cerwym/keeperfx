/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLUIRenderer.h
 *     OpenGL hardware-accelerated UI element renderer.
 * @par Purpose:
 *     Renders status sprites, floating text, slab selectors, and room flags
 *     using GPU-accelerated batched quads with proper z-ordering.
 *     Eliminates CPU staging buffer conflicts that cause UI flickering.
 */
/******************************************************************************/
#pragma once

#include "renderer/IUIRenderer.h"
#include "renderer/opengl/IGLShaderCompilable.h"
#include <vector>
#include <cstdint>

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include "renderer/ir/UICommands.h"
#include "renderer/FrameState.h"

class GLSpriteAtlas;
class GLFontAtlas;

/******************************************************************************/

/** GPU vertex for UI element quad corners. */
struct GLUIVertex {
    float x, y;       // Screen position (pixels)
    float u, v;       // Texture UV coordinates
    float r, g, b, a; // RGBA color/tint
    float z;          // NDC depth for z-ordering
    float mode;       // Render mode: 0=sprite, 1=text, 2=line, 3=solid_color
};

/** Batched UI element data before vertex expansion. */
struct UIQuad {
    float x0, y0, x1, y1;  // Screen rectangle
    float u0, v0, u1, v1;  // Texture coordinates  
    float r, g, b, a;      // Color/tint
    float z;               // Z-depth
    float mode;            // Render mode (0=sprite, 1=font, 3=solid, 10=slab, 20=colored, 30=remap)
    uint32_t texture_id;   // Sprite sheet texture ID
    int remap_row;         // Fade-table row for remap quads (mode 30); -1 = unused
};

/** Line segment for slab selectors. */
struct UILine {
    float x1, y1, x2, y2;  // Line endpoints
    float r, g, b, a;      // Line color
    float z;               // Z-depth
    float thickness;       // Line thickness
};

/** Picture-in-picture FBO composite quad.  Uses the FBO colour texture directly
 *  (no palette lookup).  Rendered before layer-1 atlas sprites so the zoom-box
 *  frame corners appear on top of the isometric PiP content. */
struct FBOQuad {
    float            x0, y0, x1, y1;   // Screen rectangle (pixels)
    GpuTextureHandle tex_id;            // RGBA8 FBO colour texture
    float            clip_radius;       // Rounded-rect corner radius; < 0 = no clip
};

/**
 * OpenGL implementation of IUIRenderer.
 * Batches UI elements and renders with GPU shaders to eliminate flickering.
 */
class GLUIRenderer : public IUIRenderer, public IGLShaderCompilable {
public:
    GLUIRenderer();
    virtual ~GLUIRenderer();

    // IUIRenderer interface
    virtual void SubmitSlabSelector(int x1, int y1, int x2, int y2, unsigned char color, float z_depth) override;
    virtual void SubmitPanelSprite(int32_t x, int32_t y, int units_per_px,
                                   SpriteHandle spr, bool flip_horiz = false) override;
    virtual void SubmitPanelSpriteRemap(int32_t x, int32_t y, int units_per_px,
                                       SpriteHandle spr, int remap_row) override;
    virtual void SubmitPanelSpriteColored(int32_t x, int32_t y, int units_per_px,
                                          SpriteHandle spr, uint8_t color_idx) override;
    virtual void SubmitScaledSprite(int32_t x, int32_t y, int32_t w, int32_t h,
                                    SpriteHandle spr) override;
    virtual void SubmitSolidBox(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx) override;
    virtual void SubmitSolidBoxAlpha(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t color_idx, float alpha) override;
    virtual void UpdateSlabTexture(const uint8_t* data, int dim) override;
    virtual void FlushPendingInit() override;
    virtual bool SubmitSlabBackground(int x, int y, int w, int h) override;
    virtual uint8_t* AcquireMinimapBuffer(int size) override;
    virtual void SubmitMinimap(int screen_x, int screen_y, int size) override;

    /** Submit an RGBA8 FBO colour texture as a picture-in-picture quad.
     *  IUIRenderer override — uses uint32_t for tex_id to avoid GL header in base. */
    void SubmitFBOQuad(int x, int y, int w, int h, GpuTextureHandle tex_id, float clip_radius = -1.0f) override;
    /** IUIRenderer override: mark start of PiP sprite capture. */
    void BeginPiPSprites() override;
    /** IUIRenderer override: flush PiP sprites into the bound FBO. */
    void DrawPiPSprites(int pip_w, int pip_h) override;
    virtual void SetLayer(int layer) override;
    virtual void SetWorldDepth(float ndc_z) override;
    virtual void ClearWorldDepth() override;
    void SetGameViewport(int x, int y, int w, int h);
    virtual void SetTopOverlay() override;
    virtual void ClearTopOverlay() override;
    virtual void DrawBack() override;
    virtual void DrawFront() override;
    /** IUIRenderer override: layer-1 portion of DrawFront: FBOQuads + layer-1 sprites
     *  + minimap + layer-1 lines.  Does NOT flush layer-2/3 or restore GL state.
     *  Must be followed by DrawFrontOverlay(). */
    void DrawFrontBase() override;
    /** IUIRenderer override: layer-2/3 portion of DrawFront: depth-tested layer-2,
     *  top-overlay layer-3, and GL state cleanup.  Call after DrawFrontBase() (and
     *  any intermediate direct-GL passes between layer-1 and layer-3). */
    void DrawFrontOverlay() override;
    /** IUIRenderer override: flip game-thread command lists to render-thread copies. */
    void FlipBuffers() override;
    virtual void Draw() override;
    virtual void Clear() override;
    virtual const char* GetName() const override { return "OPENGL_UI"; }
    const char* RendererName() const override { return "GLUIRenderer"; }

    void DrawWorldSprites() override;

    /** Compile all GLSL programs owned by this renderer.
     *  Called by the bootstrapper in RendererManager::RendererInit(). */
    bool CompileShaders() override;

    /** Flush any atlas-quad sprites submitted since the last FlushFront().
     *  Called by GLCursorLayer::Draw() to render the OS pointer sprite
     *  as the final draw before the buffer swap. */
    void DrawCursorSprites();

    /** Submit a panel sprite into the render-thread-only cursor queue.
     *  Must be called from the render thread only (e.g. from GLCursorLayer::Draw()).
     *  Pushes to m_cursor_quads instead of the shared m_quads[layer], preventing
     *  the Phase-3C data race between the render thread (cursor path) and the game
     *  thread (UI build for the next frame) on m_quads[1]. */
    void SubmitCursorPanelSprite(int32_t x, int32_t y, int units_per_px, SpriteHandle spr);

    /** Initialize OpenGL resources.
     *  @return true if successful */
    bool Init();

    /** Clean up GPU resources. */
    void Shutdown();

    /** Set screen dimensions for coordinate conversion.
     *  @param width Screen width in pixels
     *  @param height Screen height in pixels */
    void SetScreenDimensions(int width, int height);

    /** IUIRenderer override: delegates to SetScreenDimensions(). */
    void SetScreenSize(int w, int h) override { SetScreenDimensions(w, h); }

    /** Set sprite atlas for UI element textures.
     *  @param atlas Sprite atlas containing UI textures
     *  @return true if successful */
    bool SetSpriteAtlas(GLSpriteAtlas* atlas);

    /** IUIRenderer override: delegate to GLSpriteAtlas::GetSpriteMask(). */
    virtual uint8_t* QuerySpriteMask(SpriteHandle h, int* out_w, int* out_h, int* out_stride) override;

    /** Set font atlas for text rendering.
     *  @param atlas Font atlas for floating text
     *  @return true if successful */
    bool SetFontAtlas(GLFontAtlas* atlas);

    /** Supply the active 256-colour VGA palette (768 bytes: R,G,B × 256).
     *  Called by RendererOpenGL::BeginFrame().  Eliminates the direct lbPalette
     *  read from submission methods. */
    void SetPaletteSource(const uint8_t* palette) override { m_palette_data = palette; }

    /** Set palette texture for color lookups.
     *  @param palette_texture_id OpenGL texture ID for 256-color palette
     *  @return true if successful */
    bool SetPaletteTexture(GLuint palette_texture_id, GLenum target = GL_TEXTURE_2D);

    /** Set the fade-table texture for player-colour remap draws.
     *  IUIRenderer override: tex is GLuint (uint32_t) — GL context must be current. */
    void SetFadeTexture(GpuTextureHandle tex) override;

        /** Called once per frame by EndFrame(), before DrawBack().
     *  When replay=true and the queues are empty, restores the last real frame's
     *  full UI snapshot (all layers) so the sidebar stays visible during
     *  palette-fade loops.  When replay=false and queues are non-empty, saves
     *  a fresh snapshot.  Intentionally a no-op when queues are already populated
     *  (real frame with fresh content). */
    void SetStaleReplay(bool replay);

    /** Move game-thread quad/line state into m_rt_* copies; clear game buffers.
     *  Must be called with the render thread idle (i.e. just before signalling it).
     *  Called from RendererOpenGL::EndFrame(). Already declared as override above. */

    // ── IR (Intermediate Representation) path ─────────────────────────────────

    /** Set the IR write target for this frame.
     *  When @p cmds is non-null all Submit*() calls append IR commands instead
     *  of writing into m_quads[]/m_lines[]; sets cmds->ir_active = true.
     *  Call with nullptr before FlipBuffers() to close the write window (e.g.
     *  for the render-thread-only PiP path which must still use legacy quads). */
    void SetUICommandBuffers(UICommandBuffers* cmds) override;

    /** Populate m_rt_quads[]/m_rt_lines[] from the read-side IR command buffers.
     *  Must be called from the render thread, after FlushPendingInit() so that
     *  GPU resources (slab texture, sprite atlas) are ready.
     *  @param cmds  Read-side UICommandBuffers (const — render thread does not write).
     *  @param fs    FrameState snapshot for palette lookup.
     *  No-op when cmds.ir_active is false (stale-replay / fade-cache frame). */
    void ExecuteUIFromIR(const UICommandBuffers& cmds, const FrameState& fs);

    /** IUIRenderer override: delegates to ExecuteUIFromIR().
     *  Called by RenderGraph::Execute() on the render thread. */
    void PopulateFromIR(const UICommandBuffers& cmds, const FrameState& fs) override;

private:
    // GPU shader programs — one per rendering domain to isolate texture unit bindings.
    GLuint m_prog_sprite;          // palette-indexed atlas sprites (unit 0 = atlas R8, unit 1 = palette 1D)
    GLuint m_prog_sprite_colored;  // atlas-as-mask, flat vertex colour output (unit 0 = atlas R8)
    GLuint m_prog_font;     // glyph rendering (unit 0 = font atlas RGBA)
    GLuint m_prog_solid;    // solid-colour quads and lines (no textures)
    GLuint m_prog_remap;    // fade-table remap sprites (units 0 atlas, 1 palette, 2 fade)
    GLuint m_prog_fbo;      // FBO/PiP composite (unit 0: RGBA8 colour attachment)

    // Per-program u_screen_size uniform locations.
    GLint  m_loc_screen_sprite;
    GLint  m_loc_screen_sprite_colored;
    GLint  m_loc_screen_font;
    GLint  m_loc_screen_solid;
    GLint  m_loc_screen_remap;
    GLint  m_loc_remap_row;
    GLint  m_loc_screen_fbo;
    GLint  m_loc_fbo_clip_rect;
    GLint  m_loc_fbo_clip_radius;
    GLint  m_loc_fbo_clip_scrh;

    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_uniform_mvp;
    GLuint m_uniform_texture;

    // Rendering data — per-pass quad/line queues.
    // Index maps directly to render layer: 0=back, 1=front, 2=world-depth, 3=top-overlay.
    std::vector<UIQuad>     m_quads[4];     // game-thread write buffer
    std::vector<UILine>     m_lines[4];     // game-thread write buffer
    std::vector<FBOQuad>    m_fbo_quads;    // render-thread-only (PiP composite)
    std::vector<GLUIVertex> m_vertices;

    // ── Double-buffer render copies ───────────────────────────────────────────
    // FlipBuffers() moves game-thread quads/lines here before signalling the
    // render thread.  DrawBack/DrawFrontBase/DrawFrontOverlay read exclusively
    // from m_rt_*.  m_fbo_quads is NOT doubled: it is pushed AND read on the
    // render thread only (SubmitFBOQuad called during the PiP loop inside EndFrame_GL).
    // m_quads/m_lines remain in use as transient storage for PiP and cursor sprites
    // submitted by the render thread within a single EndFrame_GL call.
    std::vector<UIQuad>  m_rt_quads[4];
    std::vector<UILine>  m_rt_lines[4];

    // Render-thread-only cursor sprite buffer.  GLCursorLayer::Draw() pushes here
    // via SubmitCursorPanelSprite(); DrawCursorSprites() flushes it.  Never touched
    // by the game thread, eliminating the m_quads[1] race in Phase 3C.
    std::vector<UIQuad>  m_cursor_quads;

    // Screen properties
    int m_screen_width;
    int m_screen_height;

    // Active VGA palette (R,G,B × 256) — source pointer registered via SetPaletteSource by SetPaletteData(), eliminates lbPalette reads.
    const uint8_t* m_palette_data = nullptr;
    
    // Resources
    GLSpriteAtlas* m_sprite_atlas;
    GLFontAtlas* m_font_atlas;
    GLuint m_palette_texture;
    GLenum m_palette_texture_target;   // GL_TEXTURE_2D — set by SetPaletteTexture()
    GLuint m_fade_texture;             // R8 256×256 remap LUT — set by SetFadeTexture(), not owned

    // Slab background tile texture (64×64 R8, GL_REPEAT) — uploaded via UpdateSlabTexture()
    GLuint         m_slab_texture      = 0;
    int            m_slab_dim          = 0;
    // Pending upload latched by UpdateSlabTexture(); consumed on the render thread.
    const uint8_t* m_slab_pending_data = nullptr;
    int            m_slab_pending_dim  = 0;

    // PiP sprite watermarks: queue sizes per layer recorded at BeginPiPSprites() time.
    // Quads at indices [0..watermark[L]) in m_quads[L] were submitted before the PiP
    // draw_view (corner-frame sprites) and must survive into FlushFront() untouched.
    // Quads from [watermark[L]..end) were submitted during draw_view(pip_cam) and are
    // rendered into the FBO by DrawPiPSprites(), then erased.
    int  m_pip_quad_wm[4]      = {};
    int  m_pip_line_wm[4]      = {};
    bool m_pip_capture_active  = false;

    // Minimap: renderer-owned CPU scratch buffer + deferred GL texture upload
    uint8_t* m_minimap_cpu_buf;    // renderer-owned; returned by AcquireMinimapBuffer
    int      m_minimap_cpu_size;   // current allocation side length (0 = none)
    GLuint   m_minimap_texture;
    int      m_minimap_tex_size;   // currently allocated GL texture side (0 = none)
    int      m_minimap_x;
    int      m_minimap_y;
    int      m_minimap_size;
    bool     m_minimap_pending;
    // True if SubmitMinimap() was called at least once this frame (even if size=0
    // or buffer unavailable).  Cleared by FlipBuffers().  Allows the render thread
    // to distinguish "minimap intentionally submitted but temporarily invalid" from
    // "minimap draw path was not executed at all" (e.g. parchment map open).
    bool     m_minimap_submitted   = false;

    // Phase 3C render-thread shadow copies of minimap state.
    // FlipBuffers() swaps the CPU buffer pointers and copies metadata so the
    // render thread reads the just-completed frame while the game thread fills
    // the next frame's buffer independently (no aliasing).
    uint8_t* m_rt_minimap_cpu_buf  = nullptr;
    int      m_rt_minimap_cpu_size = 0;
    int      m_rt_minimap_x        = 0;
    int      m_rt_minimap_y        = 0;
    int      m_rt_minimap_size     = 0;
    bool     m_rt_minimap_pending  = false;
    bool     m_rt_minimap_submitted = false;
    
    // Current render layer: 0=back (before staging blit), 1=front (after staging blit),
    // 2=world-depth (after GPUFlushNow, depth test ON against tile depth buffer).
    // Set by SetLayer()/SetWorldDepth(); reset to 1 each Clear().
    int   m_current_layer       = 1;
    float m_world_z             = 0.0f;  // NDC z for active world-depth batch
    bool  m_world_depth_active  = false; // when true, SubmitQuad/SubmitLine use layer=2, z=m_world_z
    bool  m_top_overlay_active  = false; // when true, SubmitQuad/SubmitLine use layer=3 (drawn last, depth-test OFF)

    // Game viewport rect (screen pixels) — set each frame by SetGameViewport().
    // Used to scissor-clip layer-2 sprites so they don't bleed onto the sidebar,
    // overhead map, or zoom box.
    int  m_game_vp_x = 0;
    int  m_game_vp_y = 0;
    int  m_game_vp_w = 0;
    int  m_game_vp_h = 0;
    bool m_game_vp_set = false;
    int  m_rt_game_vp_x = 0;
    int  m_rt_game_vp_y = 0;
    int  m_rt_game_vp_w = 0;
    int  m_rt_game_vp_h = 0;
    bool m_rt_game_vp_set = false;

    // IR write target — set by SetUICommandBuffers(); null in stale-replay/PiP frames.
    UICommandBuffers* m_ui_write_cmds = nullptr;

    // Internal methods
    void CreateVertexArrays();
    void FlushQuads(int layer);         // Read from m_quads[layer]  (PiP/cursor transient)
    void FlushLines(int layer);         // Read from m_lines[layer]  (PiP/cursor transient)
    void FlushQuads_RT(int layer);      // Read from m_rt_quads[layer] (main frame rendering)
    void FlushLines_RT(int layer);      // Read from m_rt_lines[layer] (main frame rendering)
    void flush_quads_from(std::vector<UIQuad>& quads); // shared impl — safe to call with either buffer
    void flush_lines_from(std::vector<UILine>& lines); // shared impl — safe to call with either buffer
    void ExpandQuadToVertices(const UIQuad& quad);
    void ExpandLineToVertices(const UILine& line);
    void SubmitQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, 
                   float r, float g, float b, float a, float z, float mode, uint32_t texture_id = 0);
    void SubmitLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a, 
                   float z, float thickness = 2.0f);


};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED