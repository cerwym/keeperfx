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
#include "renderer/RenderThreadManager.h"
#include "renderer/opengl/GLWorldViewRenderer.h"
#include "renderer/opengl/GLUIRenderer.h"
#include <atomic>
#include <vector>

struct Camera;

class GLTileAtlas;
class GLSpriteAtlas;
class GLFontAtlas;
class GLMapFadePass;
class GLTextRenderer;
class ICursorLayer;

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
    void     FlushRenderWork() override;
    void     ClearScreen(uint8_t colour_index) override;
    uint8_t* LockFramebuffer(int* out_pitch) override;
    void     UnlockFramebuffer() override;
    const char* GetName() const override;
    bool     SupportsRuntimeSwitch() const override;

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
        c.supportsScreenshot      = 1;
        return c;
    }

    /** Unified full-screen image present — appends an IRImagePresentCmd to the
     *  RenderGraph write buffer (executed at EndFrame_GL by layer_z).
     *  Folds the former BlitRaw8GPU / SubmitVideoFrame / SubmitTransparentBlit /
     *  SubmitLandviewZoom GPU paths. */
    bool PresentImage(const struct RendererPresentImageDesc* desc) override;

    bool SubmitLandviewZoom(const uint8_t* src_buf, int src_w, int src_h,
                            float center_map_x, float center_map_y,
                            float screen_cx,    float screen_cy,
                            float scale) override;

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
     *  The game thread captures the PiP world/UI draw data immediately; the render
     *  thread later executes the snapshot in EndFrame(). */
    void SubmitPiPRender(struct Camera* cam, int x, int y, int w, int h) override;

    /** Called when tile/block textures are reloaded — invalidates the tile atlas
     *  so it is rebuilt at the start of the next frame. */
    void NotifyTexturesReloaded() override;

    /** Called after game tables (render_fade_tables etc.) are ready — schedules
     *  fade-table texture creation and palette wiring on the render thread. */
    void NotifyGameTablesReady() override;

    /** Schedule a screenshot: game thread queues the path; capture happens in
     *  EndFrame_GL() (render thread) after all draw calls, before the buffer swap.
     *  Returns true immediately (optimistic); render thread logs save errors. */
    bool ScheduleScreenshot(const char* path, int fmt) override;

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
    ICursorLayer*       CreateGLCursorLayer();

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
    int          m_texIndex_w   = 0; // current allocated width  (0 = not yet allocated)
    int          m_texIndex_h   = 0; // current allocated height
    unsigned int m_texPalette   = 0; // RGBA8 256×1 GL_TEXTURE_2D palette
    uint8_t      m_last_palette[768] = {}; // snapshot for dirty-flag upload
    int          m_uTintFactor  = -1; // uniform location for u_tint_factor
    int          m_uTintColor   = -1; // uniform location for u_tint_color
    int          m_uTintClipRect  = -1; // uniform location for u_clip_rect  (tint shader)
    int          m_uTintClipRadius= -1; // uniform location for u_clip_radius (tint shader)
    int          m_uTintClipScrH  = -1; // uniform location for u_clip_screen_h (tint shader)

    // Shared GPU resources (owned here, injected into world renderer)
    unsigned int m_texFade      = 0; // R8 256×256: render_fade_tables lighting LUT
    unsigned int m_tex_null     = 0; // 1×1 R8 zero — bound to sampler units lacking a real texture
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

    // ── Shared GPU resources for the unified image-present IR ─────────────
    // The R8 index texture + fullscreen quad VAO/VBO + rawblit shader are reused
    // by DrawIndexed8OpaquePresent (backgrounds/parchment/FMV) and DrawZoomPresent.
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

    // FMV per-frame embedded palette texture (256×1 RGBA8). The frame indices
    // reuse the rawblit index texture via DrawIndexed8OpaquePresent; only the
    // palette is FMV-specific (bound when a present carries PresentPalette::Embedded).
    unsigned int      m_fmv_palette_tex    = 0;  // GL_RGBA8 256×1 — per-video palette

    // ── Landview zoom GPU resources (LandviewZoom image present) ─────────
    // DrawZoomPresent uploads map_screen to m_zoom_tex and draws a fullscreen
    // quad whose fragment shader computes zoomed UVs from gl_FragCoord.
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
    // SubmitPiPRender() appends pre-captured PiP snapshots to m_pip_queue;
    // EndFrame() iterates the queue, renders each into its own FBO slot, submits
    // the colour texture to GLUIRenderer::SubmitFBOQuad() for compositing, then
    // clears the queue.
    struct PiPCmd {
        GLWorldViewRenderer::PiPCapture world_capture;
        GLUIRenderer::PiPSpriteCapture  ui_capture;
        int                             x = 0, y = 0, w = 0, h = 0;
        float                           clip_radius = -1.0f;
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
    uint8_t                     m_rt_clearColourIndex = 0;
    // Set by FlushSceneToFBO() when it has captured the IR image presents into the
    // map-fade parchment FBO, so the main EndFrame_GL pass skips re-drawing them
    // over the fade composite. Reset at the top of each EndFrame_GL(). Render
    // thread only.
    bool                        m_rt_presents_captured = false;

    // (rawblit / FMV / landview-zoom / transparent-blit render-thread copies are
    // gone — those presents ride the unified image-present IR, double-buffered by
    // RenderGraph::Flip like the UI/text command buffers.)

    // Zoom-box tile quads and background fill rects.
    std::vector<ZoomTileCmd>    m_rt_zoom_tile_cmds;
    std::vector<ZoomBoxBgCmd>   m_rt_zoom_box_bg_cmds;
    float                       m_rt_zoom_clip_rect[4] = {0,0,0,0};
    float                       m_rt_zoom_clip_radius  = -1.0f;

    // Screenshot: game thread stores pending path/fmt; EndFrame() snapshots into
    // m_rt_screenshot_* alongside other per-frame state; EndFrame_GL() captures.
    std::string                 m_pending_screenshot_path;
    int                         m_pending_screenshot_fmt  = 0;
    std::string                 m_rt_screenshot_path;
    int                         m_rt_screenshot_fmt       = 0;

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

    // scRGB fake-HDR gamma lift pass: applied before every buffer swap when the
    // backbuffer is a linear float surface.  Reads the completed SDR frame via
    // glCopyTexSubImage2D and re-draws it through sRGB→linear conversion so DWM
    // receives correct linear light values and doesn't engage SDR compensation.
    bool         m_scrgb_active      = false;
    unsigned int m_scrgb_lift_shader = 0;
    unsigned int m_scrgb_lift_tex    = 0;
    int          m_scrgb_lift_w      = 0;
    int          m_scrgb_lift_h      = 0;

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
    // entire session.  EndFrame() (game thread) calls WaitForCompletion()
    // at entry and Signal() at exit.  See RenderThreadManager.h for protocol.
    RenderThreadManager     m_render_thread;

    // Set in BeginFrame() when the animated-tile sentinel changes; consumed in
    // EndFrame_GL() on the render thread (where the GL context is current).
    // Written by game thread, read+cleared by render thread — atomic to prevent UB.
    std::atomic<bool>       m_anim_tiles_dirty {false};
    // Set in BeginFrame() when block_mem becomes available and the tile atlas has
    // not yet been GPU-initialised.  Consumed in EndFrame_GL() on the render
    // thread that owns the GL context (glGenTextures/glTexImage3D require it).
    // Written by game thread, read+cleared by render thread — atomic to prevent UB.
    std::atomic<bool>       m_tile_atlas_init_pending {false};
    // Set by ScheduleFadeTableInit() (called from RendererNotifyGameTablesReady).
    // Consumed in EndFrame_GL() — the render thread creates the GL texture then
    // pushes the ID to every sub-renderer that needs it.
    // Written by game thread, read+cleared by render thread — atomic to prevent UB.
    std::atomic<bool>       m_fade_table_pending      {false};
    bool                    m_imgui_init_pending      = false;

    /** GL submission pass — runs on the render thread.
     *  Do NOT call UIRenderer_Clear() or CursorLayer_Clear() from here;
     *  the game thread may be building the next frame concurrently once
     *  EndFrame() signals the render thread. */
    void EndFrame_GL();

    /** Execute all queued image-present commands (render thread), sorted by
     *  layer_z, dispatching on {format,kind} to the existing GL shaders. */
    void ExecuteImagePresentsFromIR(const ImagePresentBuffers& cmds);
    /** Draw one Indexed8 opaque present via the rawblit shader (render thread).
     *  Handles the game palette and the FMV embedded palette + source pitch. */
    void DrawIndexed8OpaquePresent(const IRImagePresentCmd& c);
    /** Draw a full-screen index-0-transparent overlay + possession tint
     *  (landview window frame). */
    void DrawTransparentPresent(const IRImagePresentCmd& c);
    /** Draw the landview zoom transition via the zoom fragment shader. */
    void DrawZoomPresent(const IRImagePresentCmd& c);

public:
    /** Flush all pending render commands (world geometry, raw blits, overhead
     *  map tiles) to a caller-bound FBO.  The caller must bind the FBO, set
     *  the viewport and clear it before calling.  Consumed commands will NOT
     *  be re-drawn in EndFrame().  Used by GLMapFadePass to capture views. */
    void FlushSceneToFBO(int w, int h);

};

/******************************************************************************/
