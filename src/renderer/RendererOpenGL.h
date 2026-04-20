/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererOpenGL.h
 *     OpenGL framebuffer blit renderer backend + world shader resources.
 */
/******************************************************************************/
#ifndef RENDERER_OPENGL_H
#define RENDERER_OPENGL_H

#include "IRenderer.h"
#include <vector>

// engine_camera.h defines struct Camera, which RendererOpenGL stores by value
// in PiPCmd.  The include chain is short (globals.h only) and the renderer
// already has a conceptual dependency on Camera via its C API.
#include "engine_camera.h"

class GLTileAtlas;
class GLSpriteAtlas;
class GLFontAtlas;
class GLWorldViewRenderer;

/******************************************************************************/

/**
 * OpenGL renderer backend.
 *
 * The CPU-rendered 8-bit paletted framebuffer is blitted to screen via a
 * fullscreen palette-decode shader (index texture → RGBA via 1D palette).
 *
 * Also owns the shared GPU resources (fade table texture, tile atlas,
 * palette texture) and manages all sub-renderers internally.
 */
class RendererOpenGL : public IRenderer {
private:
    class IWorldViewRenderer* m_worldViewRenderer = nullptr;
    class IMapFadePass* m_mapFadePass = nullptr;
    class ITextRenderer* m_textRenderer = nullptr;
    class IUIRenderer* m_uiRenderer = nullptr;

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

    bool SubmitOverheadMap(const uint8_t* tile_colors, int tiles_x, int tiles_y,
                           int dst_x, int dst_y, int dst_w, int dst_h) override;

    /** Queue a grid of texture-mapped tile quads for the zoom-box terrain view.
     *  @param tile_block_ids  Array of tile_block indices (from element_top_face_texture),
     *                         tiles_x × tiles_y row-major; 0xFFFF = unrevealed (skip draw).
     *  @param dst_x/dst_y     Screen top-left of the zoom-box tile grid (pixels).
     *  @param tile_w/tile_h   On-screen size of each tile (pixels). */
    void SubmitZoomBoxTiles(const uint16_t* tile_block_ids, int tiles_x, int tiles_y,
                            int dst_x, int dst_y, int tile_w, int tile_h);

    /** Schedule a picture-in-picture isometric render for draw_zoom_box (ZBM_ISOMETRIC).
     *  The camera is copied immediately; the render executes in EndFrame() after the
     *  overhead-map draw but before UIFlushFront. */
    void SubmitPiPRender(struct Camera* cam, int x, int y, int w, int h);

    // Sub-renderer access
    IWorldViewRenderer* GetWorldViewRenderer() override;
    IMapFadePass* GetMapFadePass() override;
    ITextRenderer* GetTextRenderer() override;
    IUIRenderer* GetUIRenderer() override;

    // Legacy accessors for internal resource sharing - TODO: Remove these
    GLTileAtlas*  GetTileAtlas()  const { return m_tile_atlas; }
    GLSpriteAtlas* GetSpriteAtlas() const { return m_sprite_atlas; }
    GLFontAtlas*  GetFontAtlas()  const { return m_font_atlas; }
    unsigned int  GetFadeTex()    const { return m_texFade; }
    unsigned int  GetPaletteTex() const { return m_texPalette; }

    /** Discard the tile atlas so it is rebuilt next frame with fresh block_mem data.
     *  Call after load_texture_map_file() loads new level textures. */
    void InvalidateTileAtlas();

    // Wired by RendererManager after GLWorldViewRenderer is created so that
    // EndFrame() can flush GPU geometry before the CPU blit overlay.
    void SetWorldRenderer(GLWorldViewRenderer* wr) { m_world_renderer = wr; }

private:
    bool compile_shaders();
    void upload_palette_texture();
    bool init_fade_table_texture();
    bool init_tile_atlas();

    // Screen dimensions — set in Init() once, used throughout EndFrame() for viewport sizing.
    int      m_screenW        = 0;
    int      m_screenH        = 0;
    bool     m_frame_begun     = false; // true after first BeginFrame; reset by EndFrame

    // Write-discard CPU buffer — returned by LockFramebuffer() so LbScreenLock() succeeds.
    // Content is NEVER uploaded to the GPU; this buffer exists solely to prevent null
    // dereferences in surviving CPU drawing paths (front_network, legal screen, etc.).
    // Any pixels written here are silently discarded.
    uint8_t* m_discardBuf      = nullptr;
    bool     m_discard_cleared = false;

    // Palette index requested by ClearScreen(); resolved to RGBA at glClear() time in EndFrame().
    uint8_t  m_clearColourIndex = 0;

    // GL objects — fullscreen palette-blit quad
    unsigned int m_vao          = 0;
    unsigned int m_vbo          = 0;
    unsigned int m_shader       = 0;
    unsigned int m_tintProg     = 0;  // fullscreen screen-tint overlay shader
    unsigned int m_texIndex     = 0; // GL_R8 screenW×screenH: transparent overlay texture
    unsigned int m_texPalette   = 0; // RGBA8 256-entry 1D palette
    int          m_uTintFactor  = -1; // uniform location for u_tint_factor
    int          m_uTintColor   = -1; // uniform location for u_tint_color

    // Whether a transparent overlay was submitted this frame via SubmitTransparentBlit().
    // Source data is already uploaded to m_texIndex by SubmitTransparentBlit().
    bool         m_transparent_blit_pending = false;

    // Shared GPU resources (owned here, injected into world renderer)
    unsigned int m_texFade      = 0; // R8 256×256: render_fade_tables lighting LUT
    GLTileAtlas* m_tile_atlas   = nullptr;
    GLSpriteAtlas* m_sprite_atlas = nullptr;  // UI sprite atlas
    GLFontAtlas* m_font_atlas   = nullptr;    // UI font atlas

    // Not owned — set by RendererManager after world renderer creation
    GLWorldViewRenderer* m_world_renderer = nullptr;

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
    bool              m_rawblit_cached   = false;  // last rawblit retained for palette-fade re-renders
    RawBlitCmd        m_rawblit_cached_cmd = {};
    unsigned int      m_rawblit_shader        = 0;  // palette_blit_vert + rawimage_blit_frag
    unsigned int      m_overhead_map_shader   = 0;  // palette_blit_vert + overhead_map_frag (discards idx 0)
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
    unsigned int      m_overhead_map_tex     = 0;  // GL_R8, resized to max seen
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
    // Reuses m_rawblit_vao / m_rawblit_vbo (same quad vertex layout).

    // Bounding rect of each zoom box submission — drawn as a solid black fill
    // BEFORE the tile quads so unrevealed/rock tiles show as black, not as
    // whatever is underneath (parchment, overhead map).
    struct ZoomBoxBgCmd {
        int x, y, w, h;
    };
    std::vector<ZoomBoxBgCmd> m_zoom_box_bg_cmds;

    // ── FMV video frame GPU blit (Phase C) ────────────────────────────────
    // Queued by SubmitVideoFrame(); drawn at EndFrame() using the same shader
    // as rawblit (palette_blit_vert + rawimage_blit_frag) but with a separate
    // per-video-frame palette texture instead of the game palette.
    struct FmvBlitCmd {
        const uint8_t* px       = nullptr;  // palette indices (AVFrame::data[0])
        int src_w = 0, src_h = 0, src_pitch = 0;
        const uint8_t* bgra_pal = nullptr;  // 256×BGRA (AVFrame::data[1])
        int dst_x = 0, dst_y = 0, dst_w = 0, dst_h = 0;
    };
    bool              m_fmv_pending        = false;
    FmvBlitCmd        m_fmv_cmd            = {};
    unsigned int      m_fmv_vao            = 0;
    unsigned int      m_fmv_vbo            = 0;
    unsigned int      m_fmv_index_tex      = 0;  // GL_R8 — per-frame pixel indices
    int               m_fmv_index_tex_w    = 0;
    int               m_fmv_index_tex_h    = 0;
    unsigned int      m_fmv_palette_tex    = 0;  // GL_RGBA8 1D — per-video palette

    // ── Landview zoom GPU blit (Phase D campaign-map zoom transition) ─────
    // Queued by SubmitLandviewZoom(); executed at EndFrame() using a
    // fullscreen quad whose fragment shader computes zoomed UVs from
    // gl_FragCoord rather than vertex UVs, so no geometry rebuild is needed.
    struct LandviewZoomCmd {
        const uint8_t* src_buf       = nullptr; // map_screen (8-bit indexed)
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
    // SubmitPiPRender() stores a camera snapshot and rect; EndFrame() re-runs
    // draw_view() into a dedicated FBO, then submits the colour attachment to
    // GLUIRenderer::SubmitFBOQuad() for compositing during UIFlushFront().
    struct PiPCmd {
        Camera  cam_copy;
        int     x = 0, y = 0, w = 0, h = 0;
    };
    bool              m_pip_scheduled  = false;
    PiPCmd            m_pip_cmd        = {};
    unsigned int      m_pip_fbo        = 0;   // FBO for PiP render target
    unsigned int      m_pip_color_tex  = 0;   // RGBA8 colour attachment
    unsigned int      m_pip_depth_rb   = 0;   // depth renderbuffer
    int               m_pip_fbo_w      = 0;
    int               m_pip_fbo_h      = 0;

    /** (Re-)create (or resize) the PiP FBO to at least w×h.  No-op if size matches. */
    void ensure_pip_fbo(int w, int h);

};

/******************************************************************************/
#endif // RENDERER_OPENGL_H
