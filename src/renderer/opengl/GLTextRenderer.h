/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLTextRenderer.h
 *     OpenGL hardware-accelerated text renderer.
 */
/******************************************************************************/
#pragma once

#include "renderer/ITextRenderer.h"
#include "renderer/opengl/IGLShaderCompilable.h"
#include "renderer/ir/TextCommands.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

#ifdef RENDERER_OPENGL_ENABLED

class GLFontAtlas;
class GLDbcFontAtlas;

/******************************************************************************/

/** 
 * OpenGL implementation of ITextRenderer.
 * Renders text using GPU-accelerated sprite fonts with batch processing.
 */
class GLTextRenderer : public ITextRenderer, public IGLShaderCompilable {
public:
    GLTextRenderer();
    virtual ~GLTextRenderer();

    // ITextRenderer interface — Font
    void SetFont(const struct TbSpriteSheet* font) override;
    const struct TbSpriteSheet* GetFont() const override { return m_font; }

    // ITextRenderer interface — Windowing
    void SetWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void SetJustifyWindow(int32_t x, int32_t y, int32_t w) override;
    void SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const override;
    void GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const override;

    // ITextRenderer interface — Drawing
    TbBool DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text, TbDrawFlagsMask draw_flags) override;
    TbBool DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text, TbDrawFlagsMask draw_flags) override;
    void   Draw() override;         // Flush all queued text draws
    void   SetDrawColour(uint8_t colour) override { m_text_draw_colour = colour; }
    uint8_t GetDrawColour() const override        { return m_text_draw_colour; }
    void   SetShadowColour(uint8_t colour) override { m_text_shadow_colour = colour; }
    uint8_t GetShadowColour() const override        { return m_text_shadow_colour; }

    // ── IR (Intermediate Representation) path ─────────────────────────────────

    /** Set the IR write target for this frame.
     *  When @p cmds is non-null, DrawTextResized() appends IRTextDrawCmd entries
     *  instead of DeferredDraw entries.  Sets cmds->ir_active = true.
     *  Call with nullptr before flushing (e.g. EndFrame → before FlipBuffers). */
    void SetTextCommandBuffers(TextCommandBuffers* cmds) override;

    /** Populate m_pending from the read-side IR command buffers and call Draw().
     *  Must be called from the render thread (GL context current).
     *  No-op when there are no IR draw commands this frame. */
    void ExecuteTextFromIR(const TextCommandBuffers& cmds) override;

    // ITextRenderer interface — Measurement
    int32_t LineHeight() override;
    int32_t CharWidth(uint32_t chr) override;
    int32_t CharWidthScaled(uint32_t chr, int32_t units_per_px) override;
    int32_t StringWidth(const char* text) override;
    int32_t StringWidthScaled(const char* text, int32_t units_per_px) override;
    int32_t WordWidth(const char* str) override;
    int32_t WordWidthScaled(const char* str, int32_t units_per_px) override;
    int32_t TextHeight(const char* text) override;
    int32_t StringHeight(int32_t units_per_px, const char* text) override;

    const char* GetName() const override { return "OPENGL"; }
    const char* RendererName() const override { return "GLTextRenderer"; }

    /** Initialize with OpenGL context.
     *  @return true if successful */
    bool Init();

    /** Inject the shared palette texture from RendererOpenGL (256\u00d71 GL_TEXTURE_2D RGBA8).
     *  Must be called after Init() and before the first Flush(). */
    void SetPaletteTexture(unsigned int tex) { m_palette_tex = tex; }

    /** Clean up GPU resources. */
    void Shutdown();

    /** Set screen dimensions for NDC conversion.
     *  @param width Screen width in pixels
     *  @param height Screen height in pixels */
    void SetScreenSize(int width, int height) override;

    /** Compile and link text rendering shaders, and cache uniform locations.
     *  Called by the bootstrapper in RendererManager::RendererInit().
     *  @return true if successful */
    bool CompileShaders() override;

private:
    struct DeferredDraw {
        int posx, posy, units_per_px;
        int wnd_x, wnd_y, wnd_width;        // lbTextJustifyWindow captured at queue time
        int clip_x, clip_y, clip_w, clip_h; // lbTextClipWindow captured at queue time
        unsigned char  draw_colour;         // draw colour captured at queue time
        uint32_t       draw_flags;          // draw flags captured at queue time
        std::string text;
        const struct TbSpriteSheet* font;   // Active font captured at call time
        const struct AsianFont* dbc_font;   // DBC font (nullptr when DBC off)
        uint32_t font_generation;           // RendererGetTextFontGeneration() snapshot
        long  dbc_colour0;                  // DBC face colour
        long  dbc_colour1;                  // DBC shadow colour
        TbBool dbc_enabled;                 // Whether DBC is active
    };

    std::vector<DeferredDraw>  m_pending;         // Render-thread-only: built by ExecuteTextFromIR, flushed by Draw()

    // Fallback queue for text drawn on the GAME thread while the IR write window
    // is closed (blocking loops: level load, palette fades, loading screens).
    // Guarded by m_fallback_mutex because the render thread splices it into
    // m_pending in ExecuteTextFromIR while the game thread may still be pushing.
    // Previously the game thread pushed straight into m_pending, racing the
    // render thread's Draw() and corrupting DeferredDraw::text (std::string) —
    // observed as a multi-megabyte memcpy AV during level entry.
    std::vector<DeferredDraw>  m_pending_fallback;
    std::mutex                 m_fallback_mutex;

    // IR write target — set by SetTextCommandBuffers(); null when not in IR mode.
    TextCommandBuffers* m_text_write_cmds = nullptr;

    // Per-font atlas cache: built once per unique TbSpriteSheet*, reused every frame.
    std::unordered_map<const struct TbSpriteSheet*, GLFontAtlas*> m_atlas_cache;
    GLFontAtlas*     m_active_atlas;     // Western atlas currently bound

    // Per-DBC-font atlas cache
    std::unordered_map<const struct AsianFont*, GLDbcFontAtlas*> m_dbc_atlas_cache;
    GLDbcFontAtlas*  m_active_dbc_atlas; // DBC atlas currently bound (nullptr when DBC off)

    // Font generation the atlas caches were built for.  When this diverges from
    // RendererGetTextFontGeneration() (fonts reloaded), the caches are evicted.
    uint32_t         m_cache_generation = 0;

    /** Delete every cached font atlas (Western + DBC) and clear the maps.
     *  Must run on the render thread with the GL context current. */
    void evict_font_atlas_caches();
    long             m_current_dbc_colour0; // DBC face colour for current draw
    unsigned int     m_shader_program;   // Text rendering shader
    unsigned int     m_vao;              // Vertex array object
    unsigned int     m_vbo;              // Vertex buffer object
    unsigned int     m_palette_tex;      // Current palette texture
    
    int              m_screen_width;     // Screen width for NDC conversion
    int              m_screen_height;    // Screen height for NDC conversion

    // Per-draw draw-state: set in Draw() before each TextLayout call, mutated
    // in-place by FlushSegment control codes.
    uint32_t         m_text_draw_flags  = 0;
    uint8_t          m_text_draw_colour = 0;
    uint8_t          m_text_shadow_colour = 0;
    
    // Shader uniform locations
    int              m_loc_viewport;     // u_viewport uniform
    int              m_loc_font_atlas;   // u_font_atlas uniform  
    int              m_loc_palette;      // u_palette uniform
    int              m_loc_text_color;   // u_text_color uniform

    /** Text vertex structure for GPU rendering. */
    struct TextVertex {
        float x, y;      // NDC position
        float u, v;      // Texture coordinates
        float forced_palette_idx; // >= 0: use this palette index (ONE_COLOR mode); < 0: use atlas
    };

    std::vector<TextVertex> m_vertex_batch;     // accumulated vertices, flushed in FlushBatch()
    int  m_batch_scissor_x;
    int  m_batch_scissor_y;
    int  m_batch_scissor_w;
    int  m_batch_scissor_h;
    bool m_batch_scissor_enabled;

    /** Segment callback passed to TextLayout().
     *  Casts userdata back to GLTextRenderer* and calls FlushSegment. */
    static void gl_draw_segment(const char* sbuf, const char* ebuf,
                                int32_t x, int32_t y, int32_t space_len,
                                int32_t units_per_px, void* userdata);

    /** Upload accumulated vertex batch to GPU using the current scissor state,
     *  then clear the batch.  No-op if the batch is empty. */
    void FlushBatch();

    /** Emit GPU quads for one justified line segment [sbuf, ebuf).
     *  Mirrors put_down_simpletext_sprites_resized.  Reads and updates the
     *  renderer's own draw colour/flags members for any control codes
     *  embedded in the segment (consistent with the software path).
     *  @param sbuf      Start of text segment (inclusive)
     *  @param ebuf      End of text segment (exclusive)
     *  @param screen_x  Screen-absolute X of the first character
     *  @param screen_y  Screen-absolute Y of the line
     *  @param space_len Pixel width of a space for this segment
     *  @param scale_factor units_per_px / 16.0f */
    void FlushSegment(const char* sbuf, const char* ebuf,
                      float screen_x, float screen_y,
                      float space_len, float scale_factor);

    /** Generate vertex data for a single character.
     *  @param chr Character code
     *  @param x Screen X position
     *  @param y Screen Y position  
     *  @param scale_factor Text scaling
     *  @param forced_palette_idx  >= 0: force palette index (ONE_COLOR); < 0: use atlas index
     *  @param verts Output vertex array (6 vertices for quad)
     *  @return Character width in pixels */
    int GenerateCharQuad(unsigned long chr, float x, float y, float scale_factor,
                         float forced_palette_idx, TextVertex* verts);

    /** Append a solid 1-pixel-tall coloured rect directly to the vertex batch.
     *  UV.x = -1 triggers the sentinel path in the fragment shader.
     *  y0 < y1 in screen coords (y increases downward). */
    void AppendUnderlineRect(float x0, float x1, float y0, float y1, float palette_idx);

    /** Append all underline geometry for one character cell, matching the
     *  LbDrawCharUnderline() logic: shadow first (optional), then main lines,
     *  ordered bottom-up; double underline when line_h > DOUBLE_UNDERLINE_BOUND. */
    void AppendUnderlineRects(float x0, float x1, float screen_y, float line_h);

    /** Convert screen pixel coordinates to NDC. */
    void ScreenToNDC(float screen_x, float screen_y, float* ndc_x, float* ndc_y) const;

    /** Re-issue the u_text_color uniform based on m_text_draw_flags.
     *  Call whenever m_text_draw_flags changes (per-draw and at TRANSPAR control codes). */
    void ApplyTextColorUniform();

    /**************************************************************************/
    /* Font / window state (owned, not delegated to globals)                  */
    /**************************************************************************/
    const struct TbSpriteSheet* m_font;
    TextWindow                  m_justify_window;
    TextWindow                  m_clip_window;

    // DBC state (resolved in SetFont)
    const struct AsianFont*     m_dbc_font;
    long                        m_dbc_colour0;
    long                        m_dbc_colour1;
    TbBool                      m_dbc_enabled;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED