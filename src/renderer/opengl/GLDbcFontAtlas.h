/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLDbcFontAtlas.h
 *     OpenGL font atlas for DBC (Asian double-byte) fonts.
 */
/******************************************************************************/
#pragma once

#include "bflib_basics.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "renderer/opengl/GLFontAtlas.h"  // FontGlyph

struct AsianFont;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}

/******************************************************************************/

/**
 * OpenGL font atlas for DBC (Asian double-byte character) bitmap fonts.
 * Unpacks 1-bit-per-pixel glyph bitmaps into a single GPU texture with
 * RGBA format (R=1 for foreground, A=mask).  The caller always supplies a
 * forced_palette_idx at draw time so the shader applies the correct colour.
 */
class GLDbcFontAtlas {
public:
    GLDbcFontAtlas();
    ~GLDbcFontAtlas();

    /** Build atlas from a DBC font.
     *  @param dbc_font  Loaded AsianFont with valid data pointer
     *  @return true on success */
    bool Init(const struct AsianFont* dbc_font);

    /** Release GPU resources. */
    void Shutdown();

    unsigned int GetTextureID() const { return m_texture_id; }

    /** Look up a glyph by character code.
     *  For wide characters (chr >= 0xFF), maps through dbc_char_to_font_char().
     *  For narrow characters (chr < 0xFF), uses the byte value directly.
     *  @return Glyph pointer, or nullptr if not found */
    const FontGlyph* GetGlyph(unsigned long chr) const;

    bool IsInitialized() const { return m_texture_id != 0; }

    /** Returns true when the backing AsianFont has changed since the atlas
     *  was last built (data pointer or char count differ). */
    bool NeedsRebuild(const struct AsianFont* dbc_font) const;

    int GetLineHeight() const { return m_line_height; }

private:
    unsigned int    m_texture_id;
    FontGlyph*      m_wide_glyphs;      // [chars_count] — indexed by dbc_char_to_font_char()
    FontGlyph*      m_narrow_glyphs;    // [256] — indexed by byte value
    int             m_atlas_width;
    int             m_atlas_height;
    int             m_line_height;
    unsigned long   m_built_chars_count;
    const unsigned char* m_built_data;   // data pointer at build time
    unsigned long   m_narrow_count;      // always 256

    bool PackAndUploadAtlas(const struct AsianFont* dbc_font);
};

/******************************************************************************/
#endif // __cplusplus

#endif // RENDERER_OPENGL_ENABLED
