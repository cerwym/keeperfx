/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file TileAtlasPacker.cpp
 *     Platform-independent dungeon tile atlas packer.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/TileAtlasPacker.h"

#include "engine_textures.h"   // block_ptrs[], TEXTURE_BLOCKS_COUNT, TEXTURE_*
#include "bflib_video.h"       // lbPalette (R,G,B × 256, 6-bit components)
#include "bflib_basics.h"      // ERRORLOG / SYNCLOG

#include <cstdlib>
#include <cstring>
#include "post_inc.h"

/******************************************************************************/
// Animated tile range (IDs 544–999 are live-updated each tick)
static const int k_anim_first  = TEXTURE_BLOCKS_STAT_COUNT_A;   // 544
static const int k_anim_last   = TEX_B_START_POINT;              // 999 (exclusive end)
static const int k_total_tiles = TEXTURE_BLOCKS_COUNT;           // 1544

/******************************************************************************/

TileAtlasPacker::~TileAtlasPacker()
{
    FreePacker();
}

bool TileAtlasPacker::InitPacker()
{
    if (m_rgba_scratch)
        return true; // already allocated

    const size_t sz = (size_t)k_atlas_w * k_atlas_h * 4;
    m_rgba_scratch = (uint8_t*)malloc(sz);
    if (!m_rgba_scratch)
    {
        ERRORLOG("TileAtlasPacker: failed to allocate scratch buffer (%zu bytes)", sz);
        return false;
    }
    return true;
}

void TileAtlasPacker::FreePacker()
{
    if (m_rgba_scratch)
    {
        free(m_rgba_scratch);
        m_rgba_scratch = nullptr;
    }
}

/******************************************************************************/

/*static*/
void TileAtlasPacker::GetTileUV(int tile_id,
                                 float* u0, float* v0,
                                 float* u1, float* v1)
{
    const float inv_w = 1.0f / (float)k_atlas_w;
    const float inv_h = 1.0f / (float)k_atlas_h;
    const int col = tile_id % k_atlas_cols;
    const int row = tile_id / k_atlas_cols;
    *u0 = (float)( col      * k_tile_dim) * inv_w;
    *v0 = (float)( row      * k_tile_dim) * inv_h;
    *u1 = (float)((col + 1) * k_tile_dim) * inv_w;
    *v1 = (float)((row + 1) * k_tile_dim) * inv_h;
}

/******************************************************************************/

void TileAtlasPacker::DecodeTile(const uint8_t* src_indexed, int tile_id)
{
    const int col = tile_col(tile_id);
    const int row = tile_row(tile_id);
    uint8_t* dst_row0 = m_rgba_scratch
        + (size_t)(row * k_tile_dim * k_atlas_w + col * k_tile_dim) * 4;

    for (int y = 0; y < k_tile_dim; y++)
    {
        uint8_t* dst = dst_row0 + (size_t)(y * k_atlas_w) * 4;
        for (int x = 0; x < k_tile_dim; x++)
        {
            const uint8_t idx = src_indexed[y * k_tile_dim + x];
            // lbPalette stores 6-bit R,G,B components — shift left by 2 to 8-bit
            dst[x * 4 + 0] = (uint8_t)(lbPalette[idx * 3 + 0] << 2);
            dst[x * 4 + 1] = (uint8_t)(lbPalette[idx * 3 + 1] << 2);
            dst[x * 4 + 2] = (uint8_t)(lbPalette[idx * 3 + 2] << 2);
            dst[x * 4 + 3] = 0xFF;
        }
    }
}

void TileAtlasPacker::BuildVariation(int variation)
{
    memset(m_rgba_scratch, 0, (size_t)k_atlas_w * k_atlas_h * 4);

    for (int tile_id = 0; tile_id < k_total_tiles; tile_id++)
    {
        const uint8_t* src = block_ptrs[variation * k_total_tiles + tile_id];
        if (src)
            DecodeTile(src, tile_id);
    }

    UploadFull(variation);
}

void TileAtlasPacker::BuildAnimatedStrip(int variation)
{
    const int first_row = tile_row(k_anim_first);
    const int last_row  = tile_row(k_anim_last - 1);
    const int row_count = last_row - first_row + 1;
    const int y_offset  = first_row * k_tile_dim;
    const int h_pixels  = row_count * k_tile_dim;

    // Decode the animated rows into the start of scratch (strip-local coords)
    memset(m_rgba_scratch, 0, (size_t)k_atlas_w * h_pixels * 4);

    for (int tile_id = k_anim_first; tile_id < k_anim_last; tile_id++)
    {
        const uint8_t* src = block_ptrs[variation * k_total_tiles + tile_id];
        if (!src) continue;

        const int col          = tile_col(tile_id);
        const int strip_row    = tile_row(tile_id) - first_row;
        uint8_t* dst_row0 = m_rgba_scratch
            + (size_t)(strip_row * k_tile_dim * k_atlas_w + col * k_tile_dim) * 4;

        for (int y = 0; y < k_tile_dim; y++)
        {
            uint8_t* dst = dst_row0 + (size_t)(y * k_atlas_w) * 4;
            for (int x = 0; x < k_tile_dim; x++)
            {
                const uint8_t idx = src[y * k_tile_dim + x];
                dst[x * 4 + 0] = (uint8_t)(lbPalette[idx * 3 + 0] << 2);
                dst[x * 4 + 1] = (uint8_t)(lbPalette[idx * 3 + 1] << 2);
                dst[x * 4 + 2] = (uint8_t)(lbPalette[idx * 3 + 2] << 2);
                dst[x * 4 + 3] = 0xFF;
            }
        }
    }

    UploadAnimatedStrip(variation, y_offset, h_pixels);
}
