/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererOpenGL.h
 *     OpenGL framebuffer blit renderer backend + world shader resources.
 */
/******************************************************************************/
#pragma once

#include "IRenderer.h"
#include "renderer/FrameState.h"
#include "renderer/RenderGraph.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

// engine_camera.h defines struct Camera, which RendererOpenGL stores by value
// in PiPCmd.  The include chain is short (globals.h only) and the renderer
// already has a conceptual dependency on Camera via its C API.
#include "engine_camera.h"

class GLTileAtlas;
class GLSpriteAtlas;
class GLFontAtlas;
class GLWorldViewRenderer;
class GLMapFadePass;
class GLUIRenderer;
class GLTextRenderer;

/******************************************************************************/

/**
 * OpenGL renderer backend.
 *
 * The CPU-rendered 8-bit paletted framebuffer is blitted to screen via a
 * fullscreen palette-decode shader (index texture → RGBA via 256×1 palette).
 *
 * Also owns the shared GPU resources (fade table texture, tile atlas,
 * palette texture) and manages all sub-renderers internally.
 */
class RendererOpenGL : public IRenderer {
private:
    // Typed GL sub-renderer pointers — set by RendererManager factory functions
    // after each sub-renderer is created.  Null if the sub-renderer fell back to
    // software (e.g. GLTextRenderer::Init() failure).
    GLTextRenderer*      m_textRenderer    = nullptr;
    GLMapFadePass*       m_gl_mapfade      = nullptr;
    GLUIRenderer*        m_gl_ui_renderer  = nullptr;

public:
    RendererOpenGL();
    ~RendererOpenGL() override;

    bool     Init() override;
    void     Shutdown() override;
    bool     BeginFrame() override;
    void     EndFrame() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;
    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;
    bool     SupportsGPUPasses() const override { return true; }

    BackendCapabilities GetCapabilities() const override {
        BackendCapabilities c = {};
        c.hasGPURenderPath        = 1;
        c.wantsFullscreenViewport = 1;
        c.hasGPUOverheadMap       = 1;
        c.hasGPUMapFade           = 1;
        c.hasGPUOverlay           = 1;
        c.hasGPULandviewZoom      = 1;
        c.hasGPUVideoFrame        = 1;
        c.hasGPUSprites           = 1;
        c.hasSwipeOverlay         = 1;
        c.supportsRuntimeSwitch   = 1;
        c.supportsGPUPasses       = 1;
        return c;
    }

    /** GPU raw-image blit — queues a frontend background image for opaque
     *  palette-decoded rendering at EndFrame() time.  No WScreen write occurs.
     *  Returns true unconditionally when the shader is compiled; false only
     *  when called before Init() completes (programming error). */
    bool BlitRaw8GPU(int dst_width, int dst_height, int dst_x, int dst_y,
                     const unsigned char* src_buf, int src_width, int src_height) override;

    bool SubmitVideoFrame(const uint8_t* pal8_pixels, int src_w, int src_h, int src_pitch,
                          const uint8_t* bgra_palette_1024,
                          int dst_x, int dst_y, int dst_w, int dst_h) override;

    bool SubmitLandviewZoom(const uint8_t* src_buf, int src_w, int src_h,
                            float center_map_x, float center_map_y,
                            float screen_cx,    float screen_cy,
                            float scale) override;

    bool SubmitStagingOverlay() override;

    bool SubmitTransparentBlit(const uint8_t* buf, int w, int h) override;

    void DrawSwipeOverlay(struct TbSpriteSheet* sprites, int frame,
                          bool draw_lr, int engine_window_x) override;

    bool SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                           int dst_x, int dst_y, int dst_w, int dst_h) override;

    /** Queue a grid of texture-mapped tile quads for the zoom-box terrain view.
     *  @param tile_block_ids  Array of tile_block indices (from element_top_face_texture),
     *                         tiles_x × tiles_y row-major; 0xFFFF = unrevealed (skip draw).
     *  @param dst_x/dst_y     Screen top-left of the zoom-box tile grid (pixels).
     *  @param tile_w/tile_h   On-screen size of each tile (pixels). */
    void SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                            int dst_x, int dst_y, int tile_w, int tile_h) override;

    /** Schedule a picture-in-picture isometric render for draw_zoom_box (ZBM_ISOMETRIC).
     *  The camera is copied immediately; the render executes in EndFrame() after the
     *  overhead-map draw but before UIFlushFront. */
    void SubmitPiPRender(struct Camera* cam, int x, int y, int w, int h) override;

    /** Called when tile/block textures are reloaded — invalidates the tile atlas
     *  so it is rebuilt at the start of the next frame. */
    void NotifyTexturesReloaded() override;

    /** Called after game tables (render_fade_tables etc.) are ready — schedules
     *  fade-table texture creation and palette wiring on the render thread. */
    void NotifyGameTablesReady() override;

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;

    // Sprite-atlas accessor — kept for RendererManager's sprite-handle registry.
    GLSpriteAtlas* GetSpriteAtlas() const { return m_sprite_atlas; }

    /** Discard the tile atlas so it is rebuilt next frame with fresh block_mem data.
     *  Call after load_texture_map_file() loads new level textures. */
    void InvalidateTileAtlas(); 
    
    /** Schedule the fade-table GPU texture for creation on the render thread.
     *  Must be called instead of init_fade_table_texture() once the render
     *  thread is running (i.e. after the first EndFrame()).  The actual
     *  glGenTextures / glTexImage2D runs in EndFrame_GL() where the context
     *  is current, after which SetFadeTexture() is pushed to all sub-renderers. */
    void ScheduleFadeTableInit() { m_fade_table_pending = true; }

    // Sub-renderer setters — called by RendererManager factories after creation.
    void SetWorldRenderer(GLWorldViewRenderer* wr) { m_world_renderer = wr; }
    GLWorldViewRenderer* GetWorldRenderer() const { return m_world_renderer; }

    void SetTextRenderer(GLTextRenderer* tr)   { m_textRenderer = tr; }
    void SetGLMapFadePass(GLMapFadePass* mfp)  { m_gl_mapfade = mfp; }
    void SetGLUIRenderer(GLUIRenderer* ui)     { m_gl_ui_renderer = ui; }

    // Sub-renderer factory methods — create and wire each GL sub-renderer using
    // this backend's own GPU resources.  Called by RendererManager factories so
    // that no GL resource handles need to be exposed as public accessors.
    IWorldViewRenderer* CreateGLWorldViewRenderer();
    IMapFadePass*       CreateGLMapFadePass();
    ITextRenderer*      CreateGLTextRenderer();
    /** Returns null on failure (caller should fall back to SoftwareUIRenderer). */
    IUIRenderer*        CreateGLUIRenderer();

    /** Compile GLSL programs for all GL sub-renderers.
     *  Called once by RendererManager::RendererInit() after all sub-renderers
     *  are wired up.  Returns false (and logs) if any compilation fails. */
    bool CompileSubRendererShaders();

private:
    bool compile_shaders();
    void upload_palette_texture();
    bool init_tile_atlas();
    bool init_fade_table_texture();



private:

    // Screen dimensions — set in Init() once, used throughout EndFrame() for viewport sizing.
    int      m_screenW        = 0;
    int      m_screenH        = 0;
    bool     m_frame_begun     = false; // true after first BeginFrame; reset by EndFrame

    // Palette index requested by ClearScreen(); resolved to RGBA at glClear() time in EndFrame().
    uint8_t  m_clearColourIndex = 0;

    // GL objects — fullscreen palette-blit quad
    unsigned int m_vao          = 0;
    unsigned int m_vbo          = 0;
    unsigned int m_shader       = 0;
    unsigned int m_tintProg     = 0;  // fullscreen screen-tint overlay shader
    unsigned int m_texIndex     = 0; // GL_R8 screenW×screenH: transparent overlay texture
    unsigned int m_texPalette   = 0; // RGBA8 256×1 GL_TEXTURE_2D palette
    uint8_t      m_last_palette[768] = {}; // snapshot for dirty-flag upload
    int          m_uTintFactor  = -1; // uniform location for u_tint_factor
    int          m_uTintColor   = -1; // uniform location for u_tint_color
    int          m_uTintClipRect  = -1; // uniform location for u_clip_rect  (tint shader)
    int          m_uTintClipRadius= -1; // uniform location for u_clip_radius (tint shader)
    int          m_uTintClipScrH  = -1; // uniform location for u_clip_screen_h (tint shader)

    // Whether a transparent overlay was submitted this frame via SubmitTransparentBlit().
    // Pixel data is stored in m_transparent_blit_buf and uploaded by the render thread
    // in EndFrame_GL() / FlushSceneToFBO() — never from the game thread.
    bool                  m_transparent_blit_pending = false;
    std::vector<uint8_t>  m_transparent_blit_buf;     // GL_R8 pixel data, render-thread upload

    // Shared GPU resources (owned here, injected into world renderer)
    unsigned int m_texFade      = 0; // R8 256×256: render_fade_tables lighting LUT
    GLTileAtlas* m_tile_atlas   = nullptr;
    GLSpriteAtlas* m_sprite_atlas = nullptr;  // UI sprite atlas
    GLFontAtlas* m_font_atlas   = nullptr;    // UI font atlas

    // Not owned — set by RendererManager after world renderer creation
    GLWorldViewRenderer* m_world_renderer = nullptr;
    // m_textRenderer, m_gl_mapfade, m_gl_ui_renderer declared in class head (private)

    // Sentinel for skipping redundant animated-tile atlas rebuilds.
    // block_ptrs[TEXTURE_BLOCKS_STAT_COUNT_A] changes exactly when
    // update_animating_texture_maps() advances the animation counter.
    const uint8_t* m_last_anim_sentinel = nullptr;

    // ── Raw-image GPU blit (Phase A frontend GPU path) ────────────────────
    // Queued by BlitRaw8GPU(); executed once at EndFrame() before the staging
    // blit so the opaque background composites beneath sprite overlays.
    struct RawBlitCmd {
        const uint8_t* src_buf = nullptr;
        int src_w = 0, src_h = 0;
        int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    };
    bool              m_rawblit_pending  = false;
    RawBlitCmd        m_rawblit_cmd      = {};
    std::vector<uint8_t> m_rawblit_px_buf;  ///< owns m_rawblit_cmd.src_buf data (game-thread side)
    bool              m_rawblit_cached   = false;  // last rawblit retained for palette-fade re-renders
    RawBlitCmd        m_rawblit_cached_cmd = {};
    unsigned int      m_rawblit_shader        = 0;  // palette_blit_vert + rawimage_blit_frag
    unsigned int      m_overhead_map_shader   = 0;  // palette_blit_vert + overhead_map_frag (RG8 ghost-table)
    int               m_omap_loc_map_rect   = -1;
    int               m_omap_loc_screen_size = -1;
    unsigned int      m_rawblit_vao      = 0;
    unsigned int      m_rawblit_vbo      = 0;  // GL_DYNAMIC_DRAW — updated per blit
    unsigned int      m_rawblit_tex      = 0;  // GL_R8 — source image indices
    int               m_rawblit_tex_w    = 0;  // current texture dimensions
    int               m_rawblit_tex_h    = 0;

    // ── Overhead map tile colour GPU blit ─────────────────────────────────
    // Queued by SubmitOverheadMap(); drawn as opaque rect-positioned quads
    // after the rawblit parchment background and before the staging overlay.
    // A vector supports multiple submits per frame (e.g. full map + zoom box).
    // Pixel data is copied on submit so callers may free their buffer immediately.
    struct OverheadMapCmd {
        std::vector<uint8_t> pixels;
        int tiles_x = 0, tiles_y = 0;
        int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    };
    std::vector<OverheadMapCmd> m_overhead_map_cmds;
    unsigned int      m_overhead_map_tex     = 0;  // GL_RG8, resized to max seen
    int               m_overhead_map_tex_w   = 0;
    int               m_overhead_map_tex_h   = 0;

    // ── Zoom-box tile GPU rendering ────────────────────────────────────────
    // Queued by SubmitZoomBoxTiles(); drawn at EndFrame() using the tile atlas
    // (GLTileAtlas variation 0) and the ZOOM_TILE_FRAGMENT_SHADER.  Each entry
    // represents one tile quad: UV into the tile atlas + screen destination rect.
    struct ZoomTileCmd {
        float u0, v0, u1, v1;       // normalised UV rect in the tile atlas
        int   dst_x, dst_y;         // screen top-left (pixels)
        int   dst_w, dst_h;         // tile size on screen (pixels)
    };
    std::vector<ZoomTileCmd> m_zoom_tile_cmds;
    unsigned int      m_zoom_tile_shader = 0;  // palette_blit_vert + zoom_tile_frag
    int               m_uZoomClipRect    = -1; // u_clip_rect   (zoom tile shader)
    int               m_uZoomClipRadius  = -1; // u_clip_radius (zoom tile shader)
    int               m_uZoomClipScrH    = -1; // u_clip_screen_h (zoom tile shader)
    // Reuses m_rawblit_vao / m_rawblit_vbo (same quad vertex layout).

    // Bounding rect of each zoom box submission — drawn as a solid black fill
    // BEFORE the tile quads so unrevealed/rock tiles show as black, not as
    // whatever is underneath (parchment, overhead map).
    struct ZoomBoxBgCmd {
        int x, y, w, h;
        float clip_radius;
    };
    std::vector<ZoomBoxBgCmd> m_zoom_box_bg_cmds;
    // Cached clip rect/radius from the last SubmitZoomBoxTiles — used for tile draw
    // (bg_cmds are cleared before tiles are drawn).
    float m_zoom_clip_rect[4] = {0,0,0,0};
    float m_zoom_clip_radius = -1.0f;

    // ── FMV video frame GPU blit (Phase C) ────────────────────────────────
    // Queued by SubmitVideoFrame(); drawn at EndFrame() using the same shader
    // as rawblit (palette_blit_vert + rawimage_blit_frag) but with a separate
    // per-video-frame palette texture instead of the game palette.
    // Pixel data and palette are copied into owned buffers immediately so the
    // caller (bflib_fmvids.cpp) may free the AVFrame as soon as Submit returns.
    struct FmvBlitCmd {
        const uint8_t* px       = nullptr;  // palette indices — points into owned buffer
        int src_w = 0, src_h = 0, src_pitch = 0;
        const uint8_t* bgra_pal = nullptr;  // 256×BGRA — points into owned buffer
        int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    };
    bool              m_fmv_pending        = false;
    FmvBlitCmd        m_fmv_cmd            = {};
    std::vector<uint8_t> m_fmv_px_buf;     ///< owns m_fmv_cmd.px data (game-thread side)
    std::vector<uint8_t> m_fmv_pal_buf;    ///< owns m_fmv_cmd.bgra_pal data (game-thread side)
    unsigned int      m_fmv_vao            = 0;
    unsigned int      m_fmv_vbo            = 0;
    unsigned int      m_fmv_index_tex      = 0;  // GL_R8 — per-frame pixel indices
    int               m_fmv_index_tex_w    = 0;
    int               m_fmv_index_tex_h    = 0;
    unsigned int      m_fmv_palette_tex    = 0;  // GL_RGBA8 256×1 — per-video palette

    // ── Landview zoom GPU blit (Phase D campaign-map zoom transition) ─────
    // Queued by SubmitLandviewZoom(); executed at EndFrame() using a
    // fullscreen quad whose fragment shader computes zoomed UVs from
    // gl_FragCoord rather than vertex UVs, so no geometry rebuild is needed.
    // Source pixels are copied into an owned buffer so map_screen may be written
    // freely by the game thread after Submit returns.
    struct LandviewZoomCmd {
        const uint8_t* src_buf       = nullptr; // map_screen — points into owned buffer
        int            src_w         = 0;       // LANDVIEW_MAP_WIDTH  (1280)
        int            src_h         = 0;       // LANDVIEW_MAP_HEIGHT (960)
        float          center_map_x  = 0.f;     // zoom centre, map texels
        float          center_map_y  = 0.f;
        float          screen_cx     = 0.f;     // zoom centre, screen pixels (y-down)
        float          screen_cy     = 0.f;
        float          scale         = 1.f;     // src_delta / 256.0
    };
    bool              m_zoom_pending       = false;
    LandviewZoomCmd   m_zoom_cmd           = {};
    std::vector<uint8_t> m_zoom_src_buf;   ///< owns m_zoom_cmd.src_buf data (game-thread side)
    unsigned int      m_zoom_shader        = 0;  // palette_blit_vert + landview_zoom_frag
    unsigned int      m_zoom_tex           = 0;  // GL_R8 — map_screen indices (1280×960)
    int               m_zoom_tex_w         = 0;
    int               m_zoom_tex_h         = 0;
    // Cached uniform locations for the zoom shader.
    int               m_zoom_u_center_map  = -1;
    int               m_zoom_u_screen_ctr  = -1;
    int               m_zoom_u_scale       = -1;
    int               m_zoom_u_inv_map_sz  = -1;
    int               m_zoom_u_screen_h    = -1;

    // ── Picture-in-Picture isometric render (ZBM_ISOMETRIC zoom-box mode) ─
    // SubmitPiPRender() appends to m_pip_queue; EndFrame() iterates the queue,
    // renders each into its own FBO slot, submits the colour texture to
    // GLUIRenderer::SubmitFBOQuad() for compositing, then clears the queue.
    struct PiPCmd {
        Camera  cam_copy;
        int     x = 0, y = 0, w = 0, h = 0;
        float   clip_radius = -1.0f;
    };
    struct PiPFBO {
        unsigned int fbo       = 0;
        unsigned int color_tex = 0;
        unsigned int depth_rb  = 0;
        int          w         = 0;
        int          h         = 0;
    };
    std::vector<PiPCmd> m_pip_queue;  ///< Commands accumulated this frame.
    std::vector<PiPFBO> m_pip_fbos;   ///< Per-slot FBO resources (grown on demand).

    // Phase 3C render-thread copies of per-frame command queues and scalar state.
    // EndFrame() moves/copies these before signalling the render thread so the
    // game thread (building the next frame concurrently) can safely modify the
    // originals without racing EndFrame_GL().
    std::vector<PiPCmd>         m_rt_pip_queue;
    std::vector<OverheadMapCmd> m_rt_overhead_map_cmds;
    bool                        m_rt_rawblit_pending  = false;
    RawBlitCmd                  m_rt_rawblit_cmd      = {};
    uint8_t                     m_rt_clearColourIndex = 0;

    // Render-thread copies for categories previously missing from FlipBuffers().
    // FMV: owned pixel and palette buffers so the AVFrame may be freed immediately
    // after SubmitVideoFrame() returns on the game thread.
    bool                        m_rt_fmv_pending  = false;
    FmvBlitCmd                  m_rt_fmv_cmd      = {};
    std::vector<uint8_t>        m_rt_fmv_px_buf;   ///< owns m_rt_fmv_cmd.px data
    std::vector<uint8_t>        m_rt_fmv_pal_buf;  ///< owns m_rt_fmv_cmd.bgra_pal data
    // Landview zoom: owned source-image buffer.
    bool                        m_rt_zoom_pending  = false;
    LandviewZoomCmd             m_rt_zoom_cmd      = {};
    std::vector<uint8_t>        m_rt_zoom_src_buf; ///< owns m_rt_zoom_cmd.src_buf data
    // Transparent blit (SubmitTransparentBlit): owned pixel buffer.
    bool                        m_rt_transparent_blit_pending = false;
    std::vector<uint8_t>        m_rt_transparent_blit_buf;
    // Zoom-box tile quads and background fill rects.
    std::vector<ZoomTileCmd>    m_rt_zoom_tile_cmds;
    std::vector<ZoomBoxBgCmd>   m_rt_zoom_box_bg_cmds;
    float                       m_rt_zoom_clip_rect[4] = {0,0,0,0};
    float                       m_rt_zoom_clip_radius  = -1.0f;

    /** Snapshot of all game-thread globals consumed by EndFrame_GL().
     *  Captured inside FlipBuffers() before the render thread is signalled,
     *  eliminating races between EndFrame_GL() and the next BeginFrame(). */
    FrameState                  m_rt_frame_state = {};

    // ── Unified render graph ──────────────────────────────────────────────
    // Owns all double-buffered IR command buffers (world, UI, text, shadow,
    // debug).  Game thread writes via Get*Buffers(); render thread reads after
    // Flip() via Get*BuffersRT() and Execute().
    RenderGraph                 m_render_graph;

    // Heap-allocated palette upload buffer.  Using a member instead of a stack
    // buffer in upload_palette_texture() prevents a use-after-free if the NVIDIA
    // driver's Threaded Optimisation defers reading the glTexSubImage2D data past
    // the function return and the stack frame is recycled.
    uint8_t m_palette_upload_buf[256 * 4] = {};

    // ── Lens post-process pass infrastructure ─────────────────────────────
    // Scene FBO: world geometry renders here (via FlushSceneToLensFBO) when
    // lens effects are active, instead of the default framebuffer.
    unsigned int m_lens_scene_fbo       = 0;
    unsigned int m_lens_scene_tex       = 0;  // GL_RGBA8 — decoded scene
    unsigned int m_lens_scene_depth_rb  = 0;  // GL_DEPTH_COMPONENT24
    // Ping-pong pair for chaining multiple GPU lens passes.
    unsigned int m_lens_pass_fbo_a      = 0;
    unsigned int m_lens_pass_tex_a      = 0;
    unsigned int m_lens_pass_fbo_b      = 0;
    unsigned int m_lens_pass_tex_b      = 0;
    int          m_lens_fbo_w           = 0;
    int          m_lens_fbo_h           = 0;
    // Passthrough blit shader: copies final lens texture to default framebuffer.
    unsigned int m_passthrough_shader   = 0;
    bool         m_lens_active          = false;  // true when lens FBO is bound this frame

    // ── Swipe overlay (possession attack effect) ───────────────────────────
    // Recorded by DrawSwipeOverlay() as atlas-sprite quads, flushed directly
    // by EndFrame() after GPURenderNow() while the lens FBO is still bound.
    // Entirely self-contained: own shader, VAO/VBO, no UIRenderer involvement.
    struct SwipeVertex { float x, y, u, v, r, g, b, a; };
    std::vector<SwipeVertex>  m_swipe_verts;      // game-thread write (accumulated per frame)
    std::vector<SwipeVertex>  m_rt_swipe_verts;   // render-thread read (moved from m_swipe_verts by EndFrame)
    unsigned int              m_swipe_shader = 0;
    unsigned int              m_swipe_vao    = 0;
    unsigned int              m_swipe_vbo    = 0;
    void FlushSwipeQuads();

    void ApplyLensGPUPasses();
    void EnsureLensFBOs();

    /** (Re-)create (or resize) FBO slot at index @p idx to at least w×h. */
    void ensure_pip_fbo(std::size_t idx, int w, int h);

    // ── Render thread (Phase 3A) ───────────────────────────────────────────
    // The dedicated GL-submission thread owns the OpenGL context for the
    // entire session.  EndFrame() (game thread) signals work-ready, then
    // blocks until work-done — fully synchronous in Phase 3A, establishing
    // the thread boundary without introducing any latency.
    std::thread             m_render_thread;
    std::mutex              m_rt_mutex;
    std::condition_variable m_rt_cv;
    bool                    m_rt_work_ready  = false; // game→render: EndFrame_GL() pending
    bool                    m_rt_work_done   = true;  // render→game: EndFrame_GL() complete (init true: first EndFrame() wait passes immediately)
    bool                    m_rt_quit        = false; // game→render: shut down
    bool                    m_rt_initialized = false; // render→game: context acquired
    bool                    m_rt_active      = false; // true while render thread is running

    // Set in BeginFrame() when the animated-tile sentinel changes; consumed in
    // EndFrame_GL() on the render thread (where the GL context is current).
    bool                    m_anim_tiles_dirty = false;
    // Set in BeginFrame() when block_mem becomes available and the tile atlas has
    // not yet been GPU-initialised.  Consumed in EndFrame_GL() on the render
    // thread that owns the GL context (glGenTextures/glTexImage3D require it).
    bool                    m_tile_atlas_init_pending = false;
    // Set by ScheduleFadeTableInit() (called from RendererNotifyGameTablesReady).
    // Consumed in EndFrame_GL() — the render thread creates the GL texture then
    // pushes the ID to every sub-renderer that needs it.
    bool                    m_fade_table_pending      = false;

    void RenderThreadProc(); ///< Render thread entry point.
    void EndFrame_GL();      ///< All GL submission work; runs on the render thread.

public:
    /** Flush all pending render commands (world geometry, raw blits, overhead
     *  map tiles) to a caller-bound FBO.  The caller must bind the FBO, set
     *  the viewport and clear it before calling.  Consumed commands will NOT
     *  be re-drawn in EndFrame().  Used by GLMapFadePass to capture views. */
    void FlushSceneToFBO(int w, int h);

};

/******************************************************************************/
