/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLSpriteAtlas.h
 *     Desktop OpenGL sprite texture atlas.
 * @par Purpose:
 *     Packs decoded TbSprite pixel data (palette indices) into a single
 *     4096×2048 R8 GL texture using a shelf packer.  Keyed by TbSprite*
 *     so SubmitSprite can look up UV coordinates in O(1).
 *
 *     Lifecycle mirrors IBackend: AddSheet() on load, RemoveSheet() on free.
 *     Space from removed sheets is not reclaimed (acceptable given DK's low
 *     sprite churn); the atlas is only rebuilt on full renderer reinit.
 */
/******************************************************************************/
#ifndef GL_SPRITE_ATLAS_H
#define GL_SPRITE_ATLAS_H

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include <unordered_map>
#include <vector>
#include "bflib_sprite.h"

struct SpriteUV {
    float u0, v0, u1, v1;
};

class GLSpriteAtlas {
public:
    static const int k_atlas_w = 4096;
    static const int k_atlas_h = 2048;

    GLSpriteAtlas() = default;
    ~GLSpriteAtlas() { Free(); }

    GLSpriteAtlas(const GLSpriteAtlas&)            = delete;
    GLSpriteAtlas& operator=(const GLSpriteAtlas&) = delete;

    bool Init();
    void Free();

    /** Decode and pack all sprites in a sheet into the atlas. */
    void AddSheet(const struct TbSpriteSheet* sheet);

    /** Invalidate UV entries for all sprites in a sheet. */
    void RemoveSheet(const struct TbSpriteSheet* sheet);

    /** Look up precomputed UV for a sprite pointer.  Returns false if not found. */
    bool GetUV(const struct TbSprite* spr, SpriteUV& out) const;

    GLuint GetTexture() const { return m_texture; }

private:
    bool pack_sprite(const struct TbSprite* spr, SpriteUV& out);
    void decode_rle(uint8_t* dst, int dst_stride, const struct TbSprite* spr);
    void flush_dirty();

    GLuint               m_texture = 0;
    std::vector<uint8_t> m_pixels;           // CPU copy, k_atlas_w × k_atlas_h

    // Shelf packer state
    int m_cursor_x = 0;
    int m_shelf_y  = 0;
    int m_shelf_h  = 0;

    // Dirty region for partial glTexSubImage2D upload
    int m_dirty_y_min = k_atlas_h;
    int m_dirty_y_max = -1;

    std::unordered_map<const struct TbSprite*, SpriteUV> m_uvs;
};

#endif // RENDERER_OPENGL_ENABLED
#endif // GL_SPRITE_ATLAS_H
