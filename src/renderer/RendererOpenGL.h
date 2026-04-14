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

    // Staging buffer (CPU-side, 8-bit paletted, width * height bytes)
    uint8_t* m_stagingBuf     = nullptr;
    int      m_stagingW       = 0;
    int      m_stagingH       = 0;
    bool     m_staging_cleared = false; // true after first LockFramebuffer clears for this frame
    bool     m_frame_begun     = false; // true after first BeginFrame; reset by EndFrame

    // GL objects — fullscreen palette-blit quad
    unsigned int m_vao          = 0;
    unsigned int m_vbo          = 0;
    unsigned int m_shader       = 0;
    unsigned int m_tintProg     = 0;  // fullscreen screen-tint overlay shader
    unsigned int m_texIndex     = 0; // R8: 8-bit index texture (staging upload)
    unsigned int m_texPalette   = 0; // RGBA8 256-entry 1D palette
    int          m_uTintFactor  = -1; // uniform location for u_tint_factor
    int          m_uTintColor   = -1; // uniform location for u_tint_color

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
    unsigned int      m_rawblit_shader   = 0;  // palette_blit_vert + rawimage_blit_frag
    unsigned int      m_rawblit_vao      = 0;
    unsigned int      m_rawblit_vbo      = 0;  // GL_DYNAMIC_DRAW — updated per blit
    unsigned int      m_rawblit_tex      = 0;  // GL_R8 — source image indices
    int               m_rawblit_tex_w    = 0;  // current texture dimensions
    int               m_rawblit_tex_h    = 0;

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
};

/******************************************************************************/
#endif // RENDERER_OPENGL_H
