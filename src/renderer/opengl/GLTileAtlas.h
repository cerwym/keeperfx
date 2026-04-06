/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLTileAtlas.h
 *     Desktop OpenGL tile texture atlas.
 * @par Purpose:
 *     Inherits TileAtlasPacker (shared pixel-decode + UV logic) and
 *     ITileAtlas (platform-neutral interface).  Provides desktop OpenGL 3.3
 *     texture allocation and upload implementations.
 *
 *     Layout mirrors VitaTileAtlas: 2048×1024 RGBA8 per variation (32 total).
 *     Called by GLWorldViewRenderer and RendererOpenGL.
 */
/******************************************************************************/
#ifndef GL_TILE_ATLAS_H
#define GL_TILE_ATLAS_H

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include "renderer/TileAtlasPacker.h"
#include "renderer/ITileAtlas.h"

class GLTileAtlas : public TileAtlasPacker, public ITileAtlas {
public:
    GLTileAtlas() = default;
    ~GLTileAtlas() override { Free(); }

    GLTileAtlas(const GLTileAtlas&)            = delete;
    GLTileAtlas& operator=(const GLTileAtlas&) = delete;

    // ITileAtlas
    bool         Init() override;
    void         Free() override;
    void         UpdateAnimatedTiles() override;
    unsigned int GetAtlasTexture(int variation) const override;

protected:
    // TileAtlasPacker build overrides — use R8 raw index copy instead of RGBA8 decode
    void BuildVariation(int variation) override;
    void BuildAnimatedStrip(int variation) override;

    // TileAtlasPacker upload hooks
    void UploadFull(int variation) override;
    void UploadAnimatedStrip(int variation, int y_offset, int h_pixels) override;

private:
    GLuint   m_textures[k_max_variations] = {};
    uint8_t* m_r8_scratch = nullptr;  // R8 (1 byte/pixel) CPU scratch; replaces m_rgba_scratch
};

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_TILE_ATLAS_H
