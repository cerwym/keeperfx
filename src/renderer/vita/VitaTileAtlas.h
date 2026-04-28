/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VitaTileAtlas.h
 *     Vita GPU tile texture atlas.
 * @par Purpose:
 *     Inherits TileAtlasPacker (shared pixel-decode + UV logic) and
 *     ITileAtlas (platform-neutral interface).  Provides the vitaGL
 *     texture allocation and upload implementations.
 *
 *     Layout per atlas:
 *       - 2048×1024 RGBA8 GL_TEXTURE_2D
 *       - 32×32 tiles, 64-column grid (64 cols × 25 rows = 1600 slots)
 *       - Animated tiles: IDs 544–999 re-uploaded each tick
 */
/******************************************************************************/
#pragma once

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include "renderer/TileAtlasPacker.h"
#include "renderer/ITileAtlas.h"

class VitaTileAtlas : public TileAtlasPacker, public ITileAtlas {
public:
    VitaTileAtlas() = default;
    ~VitaTileAtlas() override { Free(); }

    VitaTileAtlas(const VitaTileAtlas&)            = delete;
    VitaTileAtlas& operator=(const VitaTileAtlas&) = delete;

    // ITileAtlas
    bool         Init() override;
    void         Free() override;
    void         UpdateAnimatedTiles() override;
    unsigned int GetAtlasTexture(int variation) const override;

protected:
    // TileAtlasPacker upload hooks
    void UploadFull(int variation) override;
    void UploadAnimatedStrip(int variation, int y_offset, int h_pixels) override;

private:
    GLuint m_textures[k_max_variations] = {};
};

#endif // PLATFORM_VITA

