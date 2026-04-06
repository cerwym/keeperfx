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

    // Allocate our own R8 scratch (1 byte/pixel — palette indices only).
    // We do NOT call InitPacker() because we don't use the base-class RGBA8 decode.
    if (!m_r8_scratch)
    {
        m_r8_scratch = (uint8_t*)malloc((size_t)k_atlas_w * k_atlas_h);
        if (!m_r8_scratch)
        {
            ERRORLOG("GLTileAtlas::Init — failed to allocate R8 scratch buffer");
            return false;
        }
    }

    glGenTextures(k_max_variations, m_textures);

    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++)
    {
        glBindTexture(GL_TEXTURE_2D, m_textures[v]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        // Allocate R8 storage (zeroed) then populate via BuildVariation
        memset(m_r8_scratch, 0, (size_t)k_atlas_w * k_atlas_h);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, k_atlas_w, k_atlas_h,
                     0, GL_RED, GL_UNSIGNED_BYTE, m_r8_scratch);

        BuildVariation(v);
    }

    m_initialized = true;
    SYNCLOG("GLTileAtlas: loaded %d variations (%d×%d R8 palette-index atlas)",
            TEXTURE_VARIATIONS_COUNT, k_atlas_w, k_atlas_h);
    return true;
}

void GLTileAtlas::Free()
{
    if (m_r8_scratch)
    {
        free(m_r8_scratch);
        m_r8_scratch = nullptr;
    }
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
// TileAtlasPacker build overrides (R8 raw palette-index copy)

void GLTileAtlas::BuildVariation(int variation)
{
    memset(m_r8_scratch, 0, (size_t)k_atlas_w * k_atlas_h);

    const int total_tiles = TEXTURE_BLOCKS_COUNT;
    const int src_stride  = (int)(block_count_per_row * k_tile_dim);

    for (int tile_id = 0; tile_id < total_tiles; tile_id++)
    {
        const uint8_t* src = block_ptrs[variation * total_tiles + tile_id];
        if (!src) continue;

        const int col      = tile_id % k_atlas_cols;
        const int row      = tile_id / k_atlas_cols;
        uint8_t* dst_row0  = m_r8_scratch + (size_t)(row * k_tile_dim) * k_atlas_w
                                          + (size_t)(col * k_tile_dim);

        for (int y = 0; y < k_tile_dim; y++)
            memcpy(dst_row0 + (size_t)y * k_atlas_w, src + (size_t)y * src_stride, k_tile_dim);
    }

    UploadFull(variation);
}

void GLTileAtlas::BuildAnimatedStrip(int variation)
{
    const int first_tile  = TEXTURE_BLOCKS_STAT_COUNT_A;  // 544
    const int last_tile   = TEX_B_START_POINT;             // 999 (exclusive end)
    const int total_tiles = TEXTURE_BLOCKS_COUNT;

    const int first_row = first_tile / k_atlas_cols;
    const int last_row  = (last_tile - 1) / k_atlas_cols;
    const int y_offset  = first_row * k_tile_dim;
    const int h_pixels  = (last_row - first_row + 1) * k_tile_dim;

    memset(m_r8_scratch, 0, (size_t)k_atlas_w * h_pixels);

    const int src_stride = (int)(block_count_per_row * k_tile_dim);

    for (int tile_id = first_tile; tile_id < last_tile; tile_id++)
    {
        const uint8_t* src = block_ptrs[variation * total_tiles + tile_id];
        if (!src) continue;

        const int col       = tile_id % k_atlas_cols;
        const int strip_row = tile_id / k_atlas_cols - first_row;
        uint8_t* dst_row0   = m_r8_scratch + (size_t)(strip_row * k_tile_dim) * k_atlas_w
                                           + (size_t)(col * k_tile_dim);

        for (int y = 0; y < k_tile_dim; y++)
            memcpy(dst_row0 + (size_t)y * k_atlas_w, src + (size_t)y * src_stride, k_tile_dim);
    }

    UploadAnimatedStrip(variation, y_offset, h_pixels);
}

/******************************************************************************/
// TileAtlasPacker upload hooks

void GLTileAtlas::UploadFull(int variation)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0, k_atlas_w, k_atlas_h,
                    GL_RED, GL_UNSIGNED_BYTE,
                    m_r8_scratch);
}

void GLTileAtlas::UploadAnimatedStrip(int variation, int y_offset, int h_pixels)
{
    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, y_offset, k_atlas_w, h_pixels,
                    GL_RED, GL_UNSIGNED_BYTE,
                    m_r8_scratch);
}

#endif // RENDERER_OPENGL_ENABLED
