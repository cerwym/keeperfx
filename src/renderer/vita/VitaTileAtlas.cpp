/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaTileAtlas.cpp
 *     GPU tile texture atlas — palette-expanded dungeon tile bitmaps.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"

#include "renderer/vita/VitaTileAtlas.h"

#include "engine_textures.h"   // block_ptrs[], block_dimension, TEXTURE_*
#include "bflib_video.h"       // lbPalette (R,G,B × 256, 6-bit each)
#include "bflib_basics.h"      // SYNCLOG / ERRORLOG

#include <vitaGL.h>
#include <cstdlib>
#include <cstring>

#include "post_inc.h"

#ifdef PLATFORM_VITA

// ---------------------------------------------------------------------------
// Atlas layout constants
//
//   TEXTURE_BLOCKS_COUNT = 1544 (static-A 544 + anim 456 + static-B 544)
//   Tile size            = 32×32 pixels  (block_dimension, always 32)
//   Atlas columns        = 64   → 2048 / 32
//   Atlas rows needed    = ceil(1544 / 64) = 25  → 800 px, fits in 1024
//   Atlas size           = 2048 × 1024, RGBA8  (8 MB per variation)
//
// Animated tile IDs occupy TEXTURE_BLOCKS_STAT_COUNT_A .. TEX_B_START_POINT-1
//   = 544 .. 999 (456 tiles).  Their block_ptrs[] entries are live-updated by
//   update_animating_texture_maps() each tick.
// ---------------------------------------------------------------------------

static const int k_tile_dim    = 32;      // block_dimension (always 32)
static const int k_atlas_w     = 2048;
static const int k_atlas_h     = 1024;
static const int k_atlas_cols  = k_atlas_w / k_tile_dim;   // 64
static const int k_anim_first  = TEXTURE_BLOCKS_STAT_COUNT_A;         // 544
static const int k_anim_last   = TEX_B_START_POINT;                    // 999
static const int k_total_tiles = TEXTURE_BLOCKS_COUNT;                 // 1544

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline int tile_row(int tile_id) { return tile_id / k_atlas_cols; }
static inline int tile_col(int tile_id) { return tile_id % k_atlas_cols; }

// ---------------------------------------------------------------------------
// VitaTileAtlas
// ---------------------------------------------------------------------------

void VitaTileAtlas::GetTileUV(int tile_id,
                               float* u0, float* v0,
                               float* u1, float* v1)
{
    const float inv_w = 1.0f / (float)k_atlas_w;
    const float inv_h = 1.0f / (float)k_atlas_h;
    int col = tile_col(tile_id);
    int row = tile_row(tile_id);
    *u0 = (float)(col * k_tile_dim)       * inv_w;
    *v0 = (float)(row * k_tile_dim)       * inv_h;
    *u1 = (float)((col + 1) * k_tile_dim) * inv_w;
    *v1 = (float)((row + 1) * k_tile_dim) * inv_h;
}

GLuint VitaTileAtlas::GetAtlasTexture(int variation) const
{
    if (!m_initialized || variation < 0 || variation >= k_max_variations)
        return 0;
    return m_textures[variation];
}

bool VitaTileAtlas::Init()
{
    if (m_initialized)
        return true;

    if (block_mem == nullptr || block_ptrs[0] == nullptr) {
        ERRORLOG("VitaTileAtlas::Init — block_mem not ready");
        return false;
    }

    // Allocate the scratch buffer once (2048×1024×4 = 8 MB).
    m_rgba_scratch = (uint8_t*)malloc((size_t)k_atlas_w * k_atlas_h * 4);
    if (!m_rgba_scratch) {
        ERRORLOG("VitaTileAtlas::Init — failed to allocate scratch buffer");
        return false;
    }

    // One atlas per variation.
    glGenTextures(k_max_variations, m_textures);

    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++) {
        glBindTexture(GL_TEXTURE_2D, m_textures[v]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Allocate texture storage with a cleared image first.
        memset(m_rgba_scratch, 0, (size_t)k_atlas_w * k_atlas_h * 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, k_atlas_w, k_atlas_h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, m_rgba_scratch);

        // Fill + upload all tiles for this variation.
        BuildVariation(v, true);
    }

    m_initialized = true;
    SYNCLOG("VitaTileAtlas: loaded %d variations (%d tiles each, atlas %dx%d RGBA8)",
            TEXTURE_VARIATIONS_COUNT, k_total_tiles, k_atlas_w, k_atlas_h);
    return true;
}

void VitaTileAtlas::UpdateAnimatedTiles()
{
    if (!m_initialized) return;

    // Only the animated tile range (544..999) needs refreshing.
    // We upload only the rows that contain those tile IDs rather than the
    // full atlas, keeping the per-tick cost small (~456×4 tiles × 4 B = ~230 KB).
    const int first_row = tile_row(k_anim_first);                 // row of tile 544
    const int last_row  = tile_row(k_anim_last - 1);              // row of tile 999
    const int row_count = last_row - first_row + 1;
    const int y_offset  = first_row * k_tile_dim;
    const int h_pixels  = row_count * k_tile_dim;

    for (int v = 0; v < TEXTURE_VARIATIONS_COUNT; v++) {
        // Decode only the animated rows into the scratch buffer.
        // We reuse m_rgba_scratch as a strip whose origin is (0, y_offset)
        // so we decode into the start of the buffer and upload just that strip.
        memset(m_rgba_scratch, 0, (size_t)k_atlas_w * h_pixels * 4);

        for (int tile_id = k_anim_first; tile_id < k_anim_last; tile_id++) {
            const uint8_t* src = block_ptrs[v * k_total_tiles + tile_id];
            if (!src) continue;
            // Decode into scratch at row relative to strip start.
            int col = tile_col(tile_id);
            int row = tile_row(tile_id) - first_row;   // strip-local row
            uint8_t* dst_row0 = m_rgba_scratch
                + (size_t)(row * k_tile_dim * k_atlas_w + col * k_tile_dim) * 4;
            for (int y = 0; y < k_tile_dim; y++) {
                uint8_t* dst = dst_row0 + (size_t)(y * k_atlas_w) * 4;
                for (int x = 0; x < k_tile_dim; x++) {
                    uint8_t idx = src[y * k_tile_dim + x];
                    dst[x * 4 + 0] = (uint8_t)(lbPalette[idx * 3 + 0] << 2);
                    dst[x * 4 + 1] = (uint8_t)(lbPalette[idx * 3 + 1] << 2);
                    dst[x * 4 + 2] = (uint8_t)(lbPalette[idx * 3 + 2] << 2);
                    dst[x * 4 + 3] = 0xFF;
                }
            }
        }

        glBindTexture(GL_TEXTURE_2D, m_textures[v]);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, y_offset,
                        k_atlas_w, h_pixels,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        m_rgba_scratch);
    }
}

void VitaTileAtlas::Free()
{
    if (m_rgba_scratch) {
        free(m_rgba_scratch);
        m_rgba_scratch = nullptr;
    }
    if (m_initialized) {
        glDeleteTextures(k_max_variations, m_textures);
        memset(m_textures, 0, sizeof(m_textures));
        m_initialized = false;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void VitaTileAtlas::DecodeTile(const uint8_t* src_indexed, int tile_id)
{
    int col = tile_col(tile_id);
    int row = tile_row(tile_id);
    uint8_t* dst_row0 = m_rgba_scratch
        + (size_t)(row * k_tile_dim * k_atlas_w + col * k_tile_dim) * 4;
    for (int y = 0; y < k_tile_dim; y++) {
        uint8_t* dst = dst_row0 + (size_t)(y * k_atlas_w) * 4;
        for (int x = 0; x < k_tile_dim; x++) {
            uint8_t idx = src_indexed[y * k_tile_dim + x];
            dst[x * 4 + 0] = (uint8_t)(lbPalette[idx * 3 + 0] << 2);
            dst[x * 4 + 1] = (uint8_t)(lbPalette[idx * 3 + 1] << 2);
            dst[x * 4 + 2] = (uint8_t)(lbPalette[idx * 3 + 2] << 2);
            dst[x * 4 + 3] = 0xFF;
        }
    }
}

void VitaTileAtlas::BuildVariation(int variation, bool /*full*/)
{
    memset(m_rgba_scratch, 0, (size_t)k_atlas_w * k_atlas_h * 4);

    for (int tile_id = 0; tile_id < k_total_tiles; tile_id++) {
        const uint8_t* src = block_ptrs[variation * k_total_tiles + tile_id];
        if (!src) continue;
        DecodeTile(src, tile_id);
    }

    glBindTexture(GL_TEXTURE_2D, m_textures[variation]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, 0,
                    k_atlas_w, k_atlas_h,
                    GL_RGBA, GL_UNSIGNED_BYTE,
                    m_rgba_scratch);
}

#endif // PLATFORM_VITA
