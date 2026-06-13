/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLDbcFontAtlas.cpp
 *     OpenGL font atlas for DBC (Asian double-byte) fonts.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLDbcFontAtlas.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "bflib_sprfnt.h"

#include <glad/glad.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <limits>
#include "post_inc.h"

/******************************************************************************/

namespace {
constexpr unsigned long kMaxDbcCharsSanity = 65536;
constexpr int kMaxDbcGlyphDimSanity = 512;
constexpr int kMaxDbcAtlasSide = 4096;

inline bool IsDbcCharCountSane(unsigned long chars_count)
{
    return chars_count > 0 && chars_count <= kMaxDbcCharsSanity;
}

inline bool IsGlyphDimSane(int v)
{
    return v > 0 && v <= kMaxDbcGlyphDimSanity;
}
}

/******************************************************************************/

GLDbcFontAtlas::GLDbcFontAtlas()
    : m_texture_id(0)
    , m_wide_glyphs(nullptr)
    , m_narrow_glyphs(nullptr)
    , m_atlas_width(0)
    , m_atlas_height(0)
    , m_line_height(0)
    , m_built_chars_count(0)
    , m_built_data(nullptr)
    , m_narrow_count(256)
{
}

GLDbcFontAtlas::~GLDbcFontAtlas()
{
    Shutdown();
}

void GLDbcFontAtlas::Shutdown()
{
    if (m_texture_id != 0)
    {
        glDeleteTextures(1, &m_texture_id);
        m_texture_id = 0;
    }
    free(m_wide_glyphs);
    m_wide_glyphs = nullptr;
    free(m_narrow_glyphs);
    m_narrow_glyphs = nullptr;
    m_atlas_width = 0;
    m_atlas_height = 0;
    m_line_height = 0;
    m_built_chars_count = 0;
    m_built_data = nullptr;
}

bool GLDbcFontAtlas::Init(const struct AsianFont* dbc_font)
{
    if (!dbc_font || !dbc_font->data)
    {
        ERRORLOG("GLDbcFontAtlas: null font or data");
        return false;
    }

    if (!IsDbcCharCountSane(dbc_font->chars_count)
        || !IsGlyphDimSane((int)dbc_font->bits_width)
        || !IsGlyphDimSane((int)dbc_font->bits_height)
        || !IsGlyphDimSane((int)dbc_font->narrow_width)
        || !IsGlyphDimSane((int)dbc_font->narrow_height))
    {
        ERRORLOG("GLDbcFontAtlas: refusing init from suspicious DBC font %p (chars=%lu wide=%ux%u narrow=%ux%u)",
                 (void*)dbc_font, dbc_font->chars_count,
                 (unsigned)dbc_font->bits_width, (unsigned)dbc_font->bits_height,
                 (unsigned)dbc_font->narrow_width, (unsigned)dbc_font->narrow_height);
        return false;
    }

    m_wide_glyphs = (FontGlyph*)calloc(dbc_font->chars_count, sizeof(FontGlyph));
    m_narrow_glyphs = (FontGlyph*)calloc(256, sizeof(FontGlyph));
    if (!m_wide_glyphs || !m_narrow_glyphs)
    {
        ERRORLOG("GLDbcFontAtlas: failed to allocate glyph arrays");
        Shutdown();
        return false;
    }

    if (!PackAndUploadAtlas(dbc_font))
    {
        ERRORLOG("GLDbcFontAtlas: failed to pack and upload atlas");
        Shutdown();
        return false;
    }

    m_built_chars_count = dbc_font->chars_count;
    m_built_data = dbc_font->data;
    SYNCLOG("GLDbcFontAtlas: initialized %dx%d atlas (%lu wide + 256 narrow glyphs)",
            m_atlas_width, m_atlas_height, dbc_font->chars_count);
    return true;
}

bool GLDbcFontAtlas::NeedsRebuild(const struct AsianFont* dbc_font) const
{
    if (!dbc_font) return false;

    if (!IsDbcCharCountSane(dbc_font->chars_count)
        || !IsGlyphDimSane((int)dbc_font->bits_width)
        || !IsGlyphDimSane((int)dbc_font->bits_height)
        || !IsGlyphDimSane((int)dbc_font->narrow_width)
        || !IsGlyphDimSane((int)dbc_font->narrow_height))
    {
        ERRORLOG("GLDbcFontAtlas: suspicious DBC font in NeedsRebuild %p (chars=%lu); skipping rebuild",
                 (void*)dbc_font, dbc_font->chars_count);
        return false;
    }

    return dbc_font->chars_count != m_built_chars_count
        || dbc_font->data != m_built_data;
}

/** Unpack a single 1-bpp row-aligned glyph into the RGBA atlas buffer.
 *  Foreground pixels get R=1, A=255; background gets R=0, A=0.
 *  Row stride in source data is ceil(glyph_w / 8) bytes. */
static void UnpackGlyph(unsigned char* atlas, int atlas_stride,
                         int dst_x, int dst_y,
                         const unsigned char* src, int glyph_w, int glyph_h)
{
    int bytes_per_row = (glyph_w + 7) / 8;
    for (int y = 0; y < glyph_h; ++y)
    {
        const unsigned char* row = src + y * bytes_per_row;
        for (int x = 0; x < glyph_w; ++x)
        {
            int bit = (row[x / 8] >> (7 - (x & 7))) & 1;
            int off = ((dst_y + y) * atlas_stride + (dst_x + x)) * 4;
            atlas[off + 0] = bit ? 1 : 0;     // R = palette index (non-zero foreground)
            atlas[off + 1] = 0;
            atlas[off + 2] = 0;
            atlas[off + 3] = bit ? 255 : 0;   // A = mask
        }
    }
}

bool GLDbcFontAtlas::PackAndUploadAtlas(const struct AsianFont* dbc_font)
{
    if (!dbc_font || !dbc_font->data)
    {
        ERRORLOG("GLDbcFontAtlas: null font or data in PackAndUploadAtlas");
        return false;
    }

    int wide_w  = (int)dbc_font->bits_width;
    int wide_h  = (int)dbc_font->bits_height;
    int nar_w   = (int)dbc_font->narrow_width;
    int nar_h   = (int)dbc_font->narrow_height;
    unsigned long chars_count = dbc_font->chars_count;

    if (!IsDbcCharCountSane(chars_count)
        || !IsGlyphDimSane(wide_w) || !IsGlyphDimSane(wide_h)
        || !IsGlyphDimSane(nar_w)  || !IsGlyphDimSane(nar_h)
        || dbc_font->sdata_scanline == 0 || dbc_font->ndata_scanline == 0)
    {
        ERRORLOG("GLDbcFontAtlas: suspicious DBC font in PackAndUploadAtlas %p (chars=%lu wide=%dx%d narrow=%dx%d sscan=%u nscan=%u)",
                 (void*)dbc_font, chars_count, wide_w, wide_h, nar_w, nar_h,
                 (unsigned)dbc_font->sdata_scanline, (unsigned)dbc_font->ndata_scanline);
        return false;
    }

    m_line_height = std::max(wide_h, nar_h);

    // Calculate atlas dimensions — wide chars form a regular grid,
    // narrow chars are appended in rows below.
    int atlas_w = 2048;
    int wide_per_row = atlas_w / std::max(wide_w, 1);
    int wide_rows    = (int)((chars_count + wide_per_row - 1) / (unsigned long)wide_per_row);
    int nar_per_row  = atlas_w / std::max(nar_w, 1);
    int nar_rows     = (256 + nar_per_row - 1) / nar_per_row;

    if (wide_rows <= 0 || nar_rows <= 0)
    {
        ERRORLOG("GLDbcFontAtlas: invalid row layout for DBC atlas (wide_rows=%d nar_rows=%d)",
                 wide_rows, nar_rows);
        return false;
    }

    if (wide_rows > (std::numeric_limits<int>::max() / wide_h)
        || nar_rows > (std::numeric_limits<int>::max() / nar_h))
    {
        ERRORLOG("GLDbcFontAtlas: atlas dimension overflow risk (wide_rows=%d wide_h=%d nar_rows=%d nar_h=%d)",
                 wide_rows, wide_h, nar_rows, nar_h);
        return false;
    }

    int needed_h = wide_rows * wide_h + nar_rows * nar_h;

    // Round up to next power of two
    int atlas_h = 1;
    while (atlas_h < needed_h && atlas_h < kMaxDbcAtlasSide) atlas_h <<= 1;
    atlas_h = std::min(atlas_h, kMaxDbcAtlasSide);

    if (needed_h > atlas_h)
    {
        ERRORLOG("GLDbcFontAtlas: atlas too small (%d needed, %d max)", needed_h, atlas_h);
        return false;
    }

    m_atlas_width  = atlas_w;
    m_atlas_height = atlas_h;

    unsigned char* buf = (unsigned char*)calloc((size_t)atlas_w * atlas_h * 4, 1);
    if (!buf)
    {
        ERRORLOG("GLDbcFontAtlas: failed to allocate atlas buffer");
        return false;
    }

    float inv_w = 1.0f / (float)atlas_w;
    float inv_h = 1.0f / (float)atlas_h;

    // ---- Pack wide glyphs ----
    int cx = 0, cy = 0;
    for (unsigned long idx = 0; idx < chars_count; ++idx)
    {
        if (cx + wide_w > atlas_w)
        {
            cx = 0;
            cy += wide_h;
        }
        const unsigned char* src = dbc_font->data
            + idx * dbc_font->sdata_scanline + dbc_font->sdata_shift;
        UnpackGlyph(buf, atlas_w, cx, cy, src, wide_w, wide_h);

        m_wide_glyphs[idx].u0     = (float)cx * inv_w;
        m_wide_glyphs[idx].v0     = (float)cy * inv_h;
        m_wide_glyphs[idx].u1     = (float)(cx + wide_w) * inv_w;
        m_wide_glyphs[idx].v1     = (float)(cy + wide_h) * inv_h;
        m_wide_glyphs[idx].width  = wide_w;
        m_wide_glyphs[idx].height = wide_h;

        cx += wide_w;
    }

    // ---- Pack narrow glyphs (start on the next row after wide chars) ----
    cx = 0;
    cy = wide_rows * wide_h;
    for (int ch = 0; ch < 256; ++ch)
    {
        if (cx + nar_w > atlas_w)
        {
            cx = 0;
            cy += nar_h;
        }
        const unsigned char* src = dbc_font->data
            + (unsigned long)ch * dbc_font->ndata_scanline + dbc_font->ndata_shift;
        UnpackGlyph(buf, atlas_w, cx, cy, src, nar_w, nar_h);

        m_narrow_glyphs[ch].u0     = (float)cx * inv_w;
        m_narrow_glyphs[ch].v0     = (float)cy * inv_h;
        m_narrow_glyphs[ch].u1     = (float)(cx + nar_w) * inv_w;
        m_narrow_glyphs[ch].v1     = (float)(cy + nar_h) * inv_h;
        m_narrow_glyphs[ch].width  = nar_w;
        m_narrow_glyphs[ch].height = nar_h;

        cx += nar_w;
    }

    // Upload to GPU
    glGenTextures(1, &m_texture_id);
    glBindTexture(GL_TEXTURE_2D, m_texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas_w, atlas_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, buf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    free(buf);
    return true;
}

const FontGlyph* GLDbcFontAtlas::GetGlyph(unsigned long chr) const
{
    if (chr >= 0xFF)
    {
        // Wide character — map through the language-specific encoding table
        unsigned short font_char = dbc_char_to_font_char(chr);
        if (font_char < m_built_chars_count && m_wide_glyphs[font_char].width > 0)
            return &m_wide_glyphs[font_char];
        return nullptr;
    }

    // Narrow character
    if (m_narrow_glyphs[chr].width > 0)
        return &m_narrow_glyphs[chr];
    return nullptr;
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED
