/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaTileAtlas.h
 *     GPU tile texture atlas for the hardware rasterizer.
 * @par Purpose:
 *     Packs all dungeon tile textures (8-bit indexed, 32×32 each) into a set
 *     of RGBA8 GL texture atlases — one per ownership/variation set.
 *
 *     Layout per atlas:
 *       - 2048×1024 RGBA8 GL_TEXTURE_2D
 *       - 32×32 tiles arranged in a 64-column grid (64 cols × 25 rows = 1600)
 *       - tile_id 0..1543 → grid position (tile_id % 64, tile_id / 64)
 *       - animated tiles occupy rows covering IDs 544..999
 *
 *     Call Init() once after setup_texture_block_mem() and LbPalette is set.
 *     Call UpdateAnimatedTiles() each game tick after update_animating_texture_maps().
 *     Call Free() on shutdown.
 */
/******************************************************************************/
#ifndef VITA_TILE_ATLAS_H
#define VITA_TILE_ATLAS_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include <stdint.h>
#include <stdbool.h>

class VitaTileAtlas {
public:
    VitaTileAtlas() = default;
    ~VitaTileAtlas() { Free(); }

    VitaTileAtlas(const VitaTileAtlas&) = delete;
    VitaTileAtlas& operator=(const VitaTileAtlas&) = delete;

    /**
     * Build all variation atlases from block_ptrs[] and lbPalette.
     * Must be called after setup_texture_block_mem(), lbPalette populated,
     * and a GL context is current.
     * Returns false if texture allocation fails.
     */
    bool Init();

    /**
     * Re-upload only the animated tile rows for all variations.
     * Call this every game tick after update_animating_texture_maps().
     */
    void UpdateAnimatedTiles();

    /** Release all GL textures. Safe to call before Init(). */
    void Free();

    bool IsInitialized() const { return m_initialized; }

    /**
     * Return the GL texture handle for the given variation index (0–31).
     * Returns 0 if not initialized or out of range.
     */
    GLuint GetAtlasTexture(int variation) const;

    /**
     * Compute the UV rectangle for a tile in the atlas.
     *   u0 = left,  u1 = right
     *   v0 = top,   v1 = bottom
     */
    static void GetTileUV(int tile_id,
                          float* u0, float* v0,
                          float* u1, float* v1);

private:
    bool m_initialized = false;

    /** One GL texture per variation (TEXTURE_VARIATIONS_COUNT = 32). */
    static const int k_max_variations = 32;
    GLuint m_textures[k_max_variations] = {};

    /** Scratch RGBA buffer: 1024×1024×4 = 4 MB — allocated once on Init. */
    uint8_t* m_rgba_scratch = nullptr;

    /**
     * Expand one variation's tile set into m_rgba_scratch and upload.
     * @param variation  0..TEXTURE_VARIATIONS_COUNT-1
     * @param full       true = upload all tiles; false = animated rows only
     */
    void BuildVariation(int variation, bool full);

    /**
     * Decode one 32×32 8-bit indexed tile to RGBA8 into the scratch buffer
     * at the grid position for tile_id.
     */
    void DecodeTile(const uint8_t* src_indexed, int tile_id);
};

#endif // PLATFORM_VITA
#endif // VITA_TILE_ATLAS_H
