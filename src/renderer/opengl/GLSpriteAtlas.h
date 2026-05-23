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
#pragma once

#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "bflib_sprite.h"
#include "renderer/SpriteHandle.h"

struct SpriteUV {
    float u0, v0, u1, v1;
    uint16_t pixel_w, pixel_h;  // original sprite pixel dimensions
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

    /** Decode and pack all sprites in a sheet into the atlas.
     *  @param name  Optional debug label (e.g. "gui_panel_sprites") shown in log output. */
    void AddSheet(const struct TbSpriteSheet* sheet, const char* name = nullptr);

    /** Atomically rebuild the entire atlas: Free + Init + AddSheet for each
     *  provided sheet, all under a single exclusive lock so the render thread
     *  never sees a partially-rebuilt atlas.  Use instead of calling Free/Init/
     *  AddSheet individually when replacing the entire atlas contents. */
    void Rebuild(const struct TbSpriteSheet* const* sheets,
                 const char* const*                 names,
                 int                                count);

    /** Invalidate UV entries for all sprites in a sheet. */
    void RemoveSheet(const struct TbSpriteSheet* sheet);

    /** Look up the opaque handle assigned to spr.  Returns kInvalidSpriteHandle if not found. */
    SpriteHandle GetHandle(const struct TbSprite* spr) const;

    /** Look up precomputed UV by handle.  Returns false if invalid, out-of-range, or stale (generation mismatch after atlas rebuild). */
    bool GetUV(SpriteHandle h, SpriteUV& out) const;

    /** Look up precomputed UV by sprite pointer (convenience overload for legacy callers). */
    bool GetUV(const struct TbSprite* spr, SpriteUV& out) const;

    /** Build a packed 1-bit-per-pixel transparency mask for the sprite associated with
     *  handle h, at the sprite's native resolution.  Pixel order: left-to-right,
     *  top-to-bottom; bit 0 of byte 0 is the top-left pixel.  Opaque = 1, transparent = 0.
     *  Returns a heap-allocated buffer (caller must free() it) and writes the sprite's
     *  native pixel dimensions and the row stride in bytes into out_w / out_h / out_stride.
     *  Returns nullptr if h is invalid or the sprite is empty. */
    uint8_t* GetSpriteMask(SpriteHandle h, int* out_w, int* out_h, int* out_stride) const;

    GLuint GetTexture() const;
    size_t GetRegisteredCount() const;

    /** Perform all deferred GL work: delete old texture (if any), create the
     *  new GL texture and upload the pixel buffer packed by AddSheet() calls.
     *  Must be called from the thread that owns the GL context (render thread).
     *  Also flushes any dirty sub-regions for sprites added after the last init. */
    void FlushPendingGL();

private:
    bool pack_sprite(const struct TbSprite* spr, SpriteUV& out);
    void decode_rle(uint8_t* dst, int dst_stride, const struct TbSprite* spr);
    void flush_dirty();

    // Unlocked variants — called only while m_mutex is already held.
    SpriteHandle GetHandle_Unlocked(const struct TbSprite* spr) const;
    bool GetUV_Unlocked(SpriteHandle h, SpriteUV& out) const;
    void Free_Internal();
    bool Init_Internal();
    void AddSheet_Internal(const struct TbSpriteSheet* sheet, const char* name);

    /** Guards all cross-thread access to atlas data.
     *  Game thread holds exclusive lock during Free()/Init()/AddSheet()/RemoveSheet().
     *  Render thread holds shared lock during GetUV()/GetTexture()/GetSpriteMask().
     *  Render thread holds exclusive lock during FlushPendingGL() (writes m_texture). */
    mutable std::shared_mutex m_mutex;

    GLuint               m_texture     = 0;
    GLuint               m_old_texture = 0;  // saved in Free(); deleted in FlushPendingGL()
    bool                 m_gl_init_needed = false; // set by Init(); cleared by FlushPendingGL()
    std::vector<uint8_t> m_pixels;           // CPU copy, k_atlas_w × k_atlas_h

    // Shelf packer state
    int m_cursor_x = 0;
    int m_shelf_y  = 0;
    int m_shelf_h  = 0;

    // Dirty region for partial glTexSubImage2D upload
    int m_dirty_y_min = k_atlas_h;
    int m_dirty_y_max = -1;

    std::unordered_map<const struct TbSprite*, SpriteHandle> m_sprite_to_handle;
    std::vector<SpriteUV>                                    m_handle_uvs;  // indexed by SpriteHandleIndex(handle)
    uint32_t                                                 m_next_handle = 0;
    uint16_t                                                 m_generation  = 0;
};

#endif // RENDERER_OPENGL_ENABLED
