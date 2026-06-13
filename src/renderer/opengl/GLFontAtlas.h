/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLFontAtlas.h
 *     OpenGL font atlas for hardware-accelerated text rendering.
 */
/******************************************************************************/
#pragma once

#include "bflib_basics.h"

#ifdef RENDERER_OPENGL_ENABLED

struct TbSpriteSheet;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}

/******************************************************************************/

/** Character glyph information in the font atlas. */
struct FontGlyph {
    float u0, v0, u1, v1;    // UV coordinates in atlas texture
    int width, height;       // Character dimensions in pixels
};

/** 
 * OpenGL font atlas for sprite-based fonts.
 * Parses .tab/.dat sprite font files and uploads character data to a single
 * GPU texture, providing fast batch text rendering.
 */
class GLFontAtlas {
public:
    GLFontAtlas();
    ~GLFontAtlas();

    /** Initialize atlas from a sprite font sheet.
     *  @param font_sheet TbSpriteSheet loaded from .tab/.dat files
     *  @return true if successful */
    bool Init(const struct TbSpriteSheet* font_sheet);

    /** Clean up GPU resources. */
    void Shutdown();

    /** Get the OpenGL texture ID containing the font atlas. */
    unsigned int GetTextureID() const { return m_texture_id; }

    /** Get glyph information for a character.
     *  @param chr Character code (supports wide chars for Asian fonts)
     *  @return Pointer to glyph data, or nullptr if not found */
    const FontGlyph* GetGlyph(unsigned long chr) const;

    /** Get the line height for this font in pixels. */
    int GetLineHeight() const { return m_line_height; }

    /** Get the maximum character width in this font. */
    int GetMaxCharWidth() const { return m_max_char_width; }

    /** Check if atlas is initialized and ready to use. */
    bool IsInitialized() const { return m_texture_id != 0; }

    /** Check if the source font sheet has changed since the atlas was built.
     *  Returns true when the sprite count differs — happens when a font is
     *  loaded/reloaded after the atlas was first built from empty/partial data. */
    bool NeedsRebuild(const struct TbSpriteSheet* font_sheet) const;

    /** Get atlas texture dimensions. */
    void GetAtlasSize(int* width, int* height) const { 
        *width = m_atlas_width; 
        *height = m_atlas_height; 
    }

private:
    unsigned int m_texture_id;      // OpenGL texture ID
    FontGlyph*   m_glyphs;          // Character glyph data [256 entries]
    int          m_atlas_width;     // Atlas texture width
    int          m_atlas_height;    // Atlas texture height
    int          m_line_height;     // Line height for this font
    int          m_max_char_width;  // Maximum character width
    long         m_built_sprite_count; // num_sprites() at atlas-build time

    /** Pack sprite data into atlas and upload to GPU.
     *  @param font_sheet Source sprite font data
     *  @return true if successful */
    bool PackAndUploadAtlas(const struct TbSpriteSheet* font_sheet);

    /** Calculate optimal atlas dimensions for the given sprites. */
    void CalculateAtlasDimensions(const struct TbSpriteSheet* font_sheet, 
                                  int* width, int* height);

};

/******************************************************************************/
#endif // __cplusplus

#endif // RENDERER_OPENGL_ENABLED