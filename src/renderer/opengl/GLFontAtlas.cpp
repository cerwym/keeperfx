/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLFontAtlas.cpp
 *     OpenGL font atlas for hardware-accelerated text rendering.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLFontAtlas.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "bflib_sprite.h"
#include "bflib_sprfnt.h"
#include "bflib_basics.h"

#include <glad/glad.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "post_inc.h"

/******************************************************************************/

GLFontAtlas::GLFontAtlas()
    : m_texture_id(0)
    , m_glyphs(nullptr)
    , m_atlas_width(0)
    , m_atlas_height(0)
    , m_line_height(0)
    , m_max_char_width(0)
{
}

GLFontAtlas::~GLFontAtlas()
{
    Shutdown();
}

void GLFontAtlas::Shutdown()
{
    if (m_texture_id != 0)
    {
        glDeleteTextures(1, &m_texture_id);
        m_texture_id = 0;
    }
    
    if (m_glyphs)
    {
        free(m_glyphs);
        m_glyphs = nullptr;
    }
    
    m_atlas_width = 0;
    m_atlas_height = 0;
    m_line_height = 0;
    m_max_char_width = 0;
}

bool GLFontAtlas::Init(const struct TbSpriteSheet* font_sheet)
{
    if (!font_sheet)
    {
        ERRORLOG("GLFontAtlas: null font sheet");
        return false;
    }

    // Allocate glyph array (256 characters)
    m_glyphs = (FontGlyph*)malloc(256 * sizeof(FontGlyph));
    if (!m_glyphs)
    {
        ERRORLOG("GLFontAtlas: failed to allocate glyph array");
        return false;
    }
    memset(m_glyphs, 0, 256 * sizeof(FontGlyph));

    // Pack sprites into atlas and upload to GPU
    if (!PackAndUploadAtlas(font_sheet))
    {
        ERRORLOG("GLFontAtlas: failed to pack and upload atlas");
        Shutdown();
        return false;
    }

    SYNCLOG("GLFontAtlas: initialized %dx%d atlas with %ld characters", 
            m_atlas_width, m_atlas_height, num_sprites(font_sheet));
    return true;
}

void GLFontAtlas::CalculateAtlasDimensions(const struct TbSpriteSheet* font_sheet,
                                           int* width, int* height)
{
    // Calculate total area needed for all sprites
    long sprite_count = num_sprites(font_sheet);
    int total_area = 0;
    int max_char_width = 0;
    int max_char_height = 0;

    for (long i = 0; i < sprite_count && i < 256; ++i)
    {
        const struct TbSprite* spr = get_sprite(font_sheet, i);
        if (spr && spr->SWidth > 0 && spr->SHeight > 0)
        {
            total_area += spr->SWidth * spr->SHeight;
            max_char_width = std::max(max_char_width, (int)spr->SWidth);
            max_char_height = std::max(max_char_height, (int)spr->SHeight);
        }
    }

    m_line_height = max_char_height;
    m_max_char_width = max_char_width;

    // Estimate square dimensions with some padding
    int side = (int)sqrt(total_area * 1.5f);
    
    // Round up to next power of two for better GPU compatibility
    int atlas_width = 1;
    while (atlas_width < side) atlas_width <<= 1;
    
    int atlas_height = atlas_width;
    
    // Clamp to reasonable limits
    atlas_width = std::max(atlas_width, 256);
    atlas_height = std::max(atlas_height, 256);
    atlas_width = std::min(atlas_width, 2048);
    atlas_height = std::min(atlas_height, 2048);
    
    *width = atlas_width;
    *height = atlas_height;
}

bool GLFontAtlas::PackAndUploadAtlas(const struct TbSpriteSheet* font_sheet)
{
    // Calculate atlas dimensions
    CalculateAtlasDimensions(font_sheet, &m_atlas_width, &m_atlas_height);

    // Allocate atlas buffer (RGBA for palette lookup)
    unsigned char* atlas_buffer = (unsigned char*)malloc(m_atlas_width * m_atlas_height * 4);
    if (!atlas_buffer)
    {
        ERRORLOG("GLFontAtlas: failed to allocate atlas buffer");
        return false;
    }
    memset(atlas_buffer, 0, m_atlas_width * m_atlas_height * 4);

    // Simple packing algorithm: row-based placement
    int current_x = 0;
    int current_y = 0;
    int row_height = 0;
    
    long sprite_count = num_sprites(font_sheet);
    for (long i = 0; i < sprite_count && i < 256; ++i)
    {
        const struct TbSprite* spr = get_sprite(font_sheet, i);
        if (!spr || spr->SWidth == 0 || spr->SHeight == 0)
        {
            // Mark invalid character
            m_glyphs[i].width = 0;
            m_glyphs[i].height = 0;
            continue;
        }

        int char_width = spr->SWidth;
        int char_height = spr->SHeight;
        
        // Check if we need to wrap to next row
        if (current_x + char_width > m_atlas_width)
        {
            current_x = 0;
            current_y += row_height;
            row_height = 0;
            
            // Check if we're out of vertical space
            if (current_y + char_height > m_atlas_height)
            {
                ERRORLOG("GLFontAtlas: not enough space in atlas for all characters");
                free(atlas_buffer);
                return false;
            }
        }

        // Record glyph position and dimensions
        m_glyphs[i].u0 = (float)current_x / (float)m_atlas_width;
        m_glyphs[i].v0 = (float)current_y / (float)m_atlas_height;
        m_glyphs[i].u1 = (float)(current_x + char_width) / (float)m_atlas_width;
        m_glyphs[i].v1 = (float)(current_y + char_height) / (float)m_atlas_height;
        m_glyphs[i].width = char_width;
        m_glyphs[i].height = char_height;

        // Copy sprite data to atlas buffer
        // Decode RLE compressed sprite data
        unsigned char* char_buffer = (unsigned char*)malloc(char_width * char_height);
        if (char_buffer && DecodeRLESprite(char_buffer, char_width, spr->Data, char_width, char_height))
        {
            for (int y = 0; y < char_height; ++y)
            {
                for (int x = 0; x < char_width; ++x)
                {
                    int atlas_offset = ((current_y + y) * m_atlas_width + (current_x + x)) * 4;
                    unsigned char palette_idx = char_buffer[y * char_width + x];

                    atlas_buffer[atlas_offset + 0] = palette_idx;              // Red = palette index
                    atlas_buffer[atlas_offset + 1] = 0;                        // Green = unused
                    atlas_buffer[atlas_offset + 2] = 0;                        // Blue = unused  
                    atlas_buffer[atlas_offset + 3] = (palette_idx == 0) ? 0 : 255; // Alpha
                }
            }
        }
        if (char_buffer) free(char_buffer);

        // Update position for next character
        current_x += char_width;
        row_height = std::max(row_height, char_height);
    }

    // Create and upload OpenGL texture
    glGenTextures(1, &m_texture_id);
    glBindTexture(GL_TEXTURE_2D, m_texture_id);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_atlas_width, m_atlas_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas_buffer);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    free(atlas_buffer);
    return true;
}

const FontGlyph* GLFontAtlas::GetGlyph(unsigned long chr) const
{
    if (!m_glyphs)
        return nullptr;

    // Mirror the sprite-index mapping used by LbFontCharSprite() in bflib_sprfnt.c:
    //   chr >= 31 && chr < 256  ->  sprite[chr - 31]
    //   chr >  14 && chr <  31  ->  sprite[chr + 208]
    long idx = -1;
    if (chr >= 31 && chr < 256)
        idx = (long)chr - 31;
    else if (chr > 14 && chr < 31)
        idx = (long)chr + 208;

    if (idx >= 0 && m_glyphs[idx].width > 0)
        return &m_glyphs[idx];

    return nullptr;
}

bool GLFontAtlas::DecodeRLESprite(unsigned char* dst, int dst_stride,
                                 const unsigned char* sprite_data, int w, int h)
{
    if (!dst || !sprite_data || w <= 0 || h <= 0)
        return false;

    // Clear destination buffer
    for (int y = 0; y < h; ++y)
        memset(dst + y * dst_stride, 0, w);

    const signed char* sp = reinterpret_cast<const signed char*>(sprite_data);
    for (int y = 0; y < h; ++y) {
        unsigned char* row = dst + y * dst_stride;
        int x = 0;
        while (true) {
            signed char cmd = *sp++;
            if (cmd == 0) break; // End of row
            if (cmd < 0) {
                // Transparent skip
                x += (int)(-cmd);
            } else {
                // Run of palette bytes
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
                    if (x < w) row[x] = (unsigned char)(*sp);
                    ++sp;
                    ++x;
                }
            }
        }
    }
    return true;
}

/******************************************************************************/

#endif // RENDERER_OPENGL_ENABLED