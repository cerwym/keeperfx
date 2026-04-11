/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLTextRenderer.h
 *     OpenGL hardware-accelerated text renderer.
 */
/******************************************************************************/
#ifndef GLTEXT_RENDERER_H
#define GLTEXT_RENDERER_H

#include "renderer/ITextRenderer.h"
#include <string>
#include <unordered_map>
#include <vector>

#ifdef RENDERER_OPENGL_ENABLED

class GLFontAtlas;

/******************************************************************************/

/** 
 * OpenGL implementation of ITextRenderer.
 * Renders text using GPU-accelerated sprite fonts with batch processing.
 */
class GLTextRenderer : public ITextRenderer {
public:
    GLTextRenderer();
    virtual ~GLTextRenderer();

    // ITextRenderer interface — Font
    void SetFont(const struct TbSpriteSheet* font) override;

    // ITextRenderer interface — Windowing
    void SetWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void SetJustifyWindow(int32_t x, int32_t y, int32_t w) override;
    void SetClipWindow(int32_t x, int32_t y, int32_t w, int32_t h) override;
    void GetJustifyWindow(int32_t* x, int32_t* y, int32_t* w) const override;
    void GetClipWindow(int32_t* x, int32_t* y, int32_t* w, int32_t* h) const override;

    // ITextRenderer interface — Drawing
    TbBool DrawTextResized(int32_t posx, int32_t posy, int32_t units_per_px, const char* text) override;
    TbBool DrawTextAt(int32_t screen_x, int32_t screen_y, int32_t units_per_px, const char* text) override;
    void   Flush() override;

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

    /** Initialize with OpenGL context.
     *  @return true if successful */
    bool Init();

    /** Clean up GPU resources. */
    void Shutdown();

    /** Set screen dimensions for NDC conversion.
     *  @param width Screen width in pixels
     *  @param height Screen height in pixels */
    void SetScreenSize(int width, int height);

private:
    struct DeferredDraw {
        int posx, posy, units_per_px;
        int wnd_x, wnd_y, wnd_width;        // lbTextJustifyWindow captured at queue time
        int clip_x, clip_y, clip_w, clip_h; // lbTextClipWindow captured at queue time
        unsigned char  draw_colour;         // lbDisplay.DrawColour at queue time
        unsigned short draw_flags;          // lbDisplay.DrawFlags  at queue time
        std::string text;
        const struct TbSpriteSheet* font;   // lbFontPtr captured at call time
    };

    std::vector<DeferredDraw>  m_pending;         // Queued draws, flushed in Flush()

    // Per-font atlas cache: built once per unique TbSpriteSheet*, reused every frame.
    std::unordered_map<const struct TbSpriteSheet*, GLFontAtlas*> m_atlas_cache;
    GLFontAtlas*     m_active_atlas;     // Atlas currently bound for the in-progress draw
    unsigned int     m_shader_program;   // Text rendering shader
    unsigned int     m_vao;              // Vertex array object
    unsigned int     m_vbo;              // Vertex buffer object
    unsigned int     m_palette_tex;      // Current palette texture
    
    int              m_screen_width;     // Screen width for NDC conversion
    int              m_screen_height;    // Screen height for NDC conversion
    
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

    /** C-linkage-compatible segment callback passed to LbTextLayout.
     *  Casts userdata back to GLTextRenderer* and calls FlushSegment. */
    static void gl_draw_segment(const char* sbuf, const char* ebuf,
                                long x, long y, long space_len,
                                int units_per_px, void* userdata);

    /** Compile and link text rendering shaders.
     *  @return true if successful */
    bool CompileShaders();

    /** Emit GPU quads for one justified line segment [sbuf, ebuf).
     *  Mirrors put_down_simpletext_sprites_resized.  Reads lbDisplay.DrawColour
     *  and lbDisplay.DrawFlags as globals and updates them for any control codes
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

    /** Update palette texture from current game palette. */
    void UpdatePaletteTexture();

    /** Convert screen pixel coordinates to NDC. */
    void ScreenToNDC(float screen_x, float screen_y, float* ndc_x, float* ndc_y) const;
};

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
#endif // GLTEXT_RENDERER_H