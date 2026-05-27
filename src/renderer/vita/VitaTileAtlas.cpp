/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaTileAtlas.cpp
 *     Vita GPU tile texture atlas — vitaGL upload implementation.
 * @par Purpose:
 *     Provides the vitaGL texture allocation and upload hooks for
 *     TileAtlasPacker.  All pixel decoding and UV math live in
 *     TileAtlasPacker (platform-independent).
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"

#include "renderer/vita/VitaTileAtlas.h"
#include "engine_textures.h"   // TEXTURE_VARIATIONS_COUNT
#include "bflib_basics.h"      // SYNCLOG / ERRORLOG

#include <vitaGL.h>
#include <cstring>
#include "post_inc.h"

#ifdef PLATFORM_VITA

/******************************************************************************/

bool VitaTileAtlas::Init()
{
    if (m_initialized)
        return true;

    if (block_mem == nullptr || block_ptrs[0] == nullptr)
    {
        ERRORLOG("VitaTileAtlas::Init — block_mem not ready");
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Allocate storage (zeroed) then populate via BuildVariation
        memset(m_rgba_scratch, 0, (size_t)k_atlas_w * k_atlas_h * 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, k_atlas_w, k_atlas_h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, m_rgba_scratch);

        BuildVariation(v);
    }

    m_initialized = true;
    SYNCLOG("VitaTileAtlas: loaded %d variations (%d×%d RGBA8 atlas)",
            TEXTURE_VARIATIONS_COUNT, k_atlas_w, k_atlas_h);
    return true;
}

void VitaTileAtlas::Free()
{
    FreePacker();
    if (m_initialized)
    {
        glDeleteTextures(k_max_variations, m_textures);
        memset(m_textures, 0, sizeof(m_textures));
        m_initialized = false;
    }
}

void VitaTileAtlas::UpdateAnimatedTiles()
{
    if (!m_initialized) return;
    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++)
        BuildAnimatedStrip(v);
}

unsigned int VitaTileAtlas::GetAtlasTexture(int variation) const
{
    if (!m_initialized || variation < 0 || variation >= k_max_variations)
        return 0;
    return (unsigned int)m_textures[variation];
}

/******************************************************************************/
// TileAtlasPacker upload hooks

void VitaTileAtlas::UploadFull(int variation)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0, k_atlas_w, k_atlas_h,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_rgba_scratch);
}

void VitaTileAtlas::UploadAnimatedStrip(int variation, int y_offset, int h_pixels)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, y_offset, k_atlas_w, h_pixels,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_rgba_scratch);
}

#endif // PLATFORM_VITA

