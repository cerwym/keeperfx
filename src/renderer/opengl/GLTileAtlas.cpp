/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLTileAtlas.cpp
 *     Desktop OpenGL tile texture atlas — GL upload implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLTileAtlas.h"

#ifdef RENDERER_OPENGL_ENABLED

#include "engine_textures.h"   // TEXTURE_VARIATIONS_COUNT
#include "bflib_basics.h"      // SYNCLOG / ERRORLOG

#include <glad/glad.h>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/

bool GLTileAtlas::Init()
{
    if (m_initialized)
        return true;

    if (block_mem == nullptr || block_ptrs[0] == nullptr)
    {
        ERRORLOG("GLTileAtlas::Init — block_mem not ready");
        return false;
    }

    if (!InitPacker())
        return false;

    glGenTextures(k_max_variations, m_textures);

    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++)
    {
        glBindTexture(GL_TEXTURE_2D, m_textures[v]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        // Allocate storage (zeroed) then populate via BuildVariation
        memset(m_rgba_scratch, 0, (size_t)k_atlas_w * k_atlas_h * 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, k_atlas_w, k_atlas_h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, m_rgba_scratch);

        BuildVariation(v);
    }

    m_initialized = true;
    SYNCLOG("GLTileAtlas: loaded %d variations (%d×%d RGBA8 atlas)",
            TEXTURE_VARIATIONS_COUNT, k_atlas_w, k_atlas_h);
    return true;
}

void GLTileAtlas::Free()
{
    FreePacker();
    if (m_initialized)
    {
        glDeleteTextures(k_max_variations, m_textures);
        memset(m_textures, 0, sizeof(m_textures));
        m_initialized = false;
    }
}

void GLTileAtlas::UpdateAnimatedTiles()
{
    if (!m_initialized) return;
    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++)
        BuildAnimatedStrip(v);
}

unsigned int GLTileAtlas::GetAtlasTexture(int variation) const
{
    if (!m_initialized || variation < 0 || variation >= k_max_variations)
        return 0;
    return (unsigned int)m_textures[variation];
}

/******************************************************************************/
// TileAtlasPacker upload hooks

void GLTileAtlas::UploadFull(int variation)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0, k_atlas_w, k_atlas_h,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_rgba_scratch);
}

void GLTileAtlas::UploadAnimatedStrip(int variation, int y_offset, int h_pixels)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, y_offset, k_atlas_w, h_pixels,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_rgba_scratch);
}

#endif // RENDERER_OPENGL_ENABLED
