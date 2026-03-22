/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file UISpriteAtlas.h
 *     GPU texture atlas for UI sprites (Tier 1 — screen-space).
 * @par Purpose:
 *     Decodes RLE-encoded, 8-bit palettized TbSprite data into RGBA8 texels
 *     at sheet-load time and packs them into one or more 2048×2048 GL texture
 *     pages using a simple shelf packer.
 *
 *     Key properties:
 *       - All palette expansion happens once at load time (6-bit → 8-bit,
 *         RLE transparency → alpha=0, opaque pixels → alpha=255).
 *       - Append-only for Tier 1: sprites are never evicted from the atlas
 *         mid-session. A sheet freed via on_sheet_freed() simply invalidates
 *         its UV entries so future lookups return false (GPU still holds the
 *         texels, which is harmless — the atlas is rebuilt on the next Init()).
 *       - Atlas page allocation is deferred to Init(); the class is safe to
 *         construct before a GL context exists.
 */
/******************************************************************************/
#ifndef UI_SPRITE_ATLAS_H
#define UI_SPRITE_ATLAS_H

#ifdef PLATFORM_VITA

#include <vitaGL.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>

struct TbSprite;
struct TbSpriteSheet;

/** Maximum number of 2048×2048 RGBA8 atlas pages.
 *  At ~16 MB per page this comfortably fits in Vita CDRAM (256 MB). */
static const int k_atlas_max_pages = 4;

/** Side length of each atlas page, in texels. */
static const int k_atlas_page_size = 2048;

/** Pixel padding between packed sprites (prevents bilinear bleed). */
static const int k_atlas_padding = 1;

/******************************************************************************/

/** UV rectangle on a specific atlas page (all values in [0, 1]). */
struct AtlasEntry {
    float u0, v0, u1, v1;
    int   page;        /**< Index into m_pages[]. */
    bool  valid;       /**< false if the owning sheet was freed. */
};

/******************************************************************************/

class UISpriteAtlas {
public:
    UISpriteAtlas()  = default;
    ~UISpriteAtlas() { Free(); }

    UISpriteAtlas(const UISpriteAtlas&)            = delete;
    UISpriteAtlas& operator=(const UISpriteAtlas&) = delete;

    /** Allocate GL texture pages.  Must be called with a GL context current.
     *  Safe to call multiple times (idempotent). */
    bool Init();

    /** Upload all sprites from a newly loaded TbSpriteSheet.
     *  Stores raw palette indices — no palette required at load time.
     *  The palette is applied at render time via the palette lookup texture.
     *  @param sheet  Pointer used as a lookup key — must be stable. */
    void AddSheet(const TbSpriteSheet* sheet);

    /** Invalidate all entries belonging to sheet — does NOT touch GL memory. */
    void RemoveSheet(const TbSpriteSheet* sheet);

    /** Look up the packed UV rect for a sprite.
     *  Keyed by the sprite's Data pointer — unique and stable per sprite.
     *  @param spr  Sprite to look up.
     *  @param out  Filled if found and valid.
     *  @return true if the sprite is in the atlas, false (caller should SW-fallback). */
    bool GetUV(const TbSprite* spr, AtlasEntry* out) const;

    /** Return the GL texture handle for atlas page @p page. */
    GLuint GetTexture(int page) const;

    /** Is the atlas ready to use? */
    bool IsInitialized() const { return m_initialized; }

    /** Release all GL resources and clear the lookup table. */
    void Free();

private:
    bool m_initialized = false;

    /* --- GL textures, one per page — GL_LUMINANCE_ALPHA (index, alpha) --- */
    GLuint m_textures[k_atlas_max_pages] = {};
    int    m_page_count = 0;

    /* --- Current packing cursor (shelf packer) --- */
    int m_cur_page        = 0;   /**< Active page index. */
    int m_shelf_x         = 0;   /**< Next X on the current shelf. */
    int m_shelf_y         = 0;   /**< Top Y of the current shelf. */
    int m_shelf_h         = 0;   /**< Height of the current shelf. */

    /* --- Lookup: key = sprite Data pointer (unique per sprite) → AtlasEntry --- */
    std::unordered_map<uintptr_t, AtlasEntry> m_entries;

    /* --- Reverse index for RemoveSheet: sheet ptr → list of Data ptr keys --- */
    std::unordered_map<uintptr_t, std::vector<uintptr_t>> m_sheet_keys;

    /* --- Scratch buffer: 2 bytes per pixel [palette_index, alpha] --- */
    static const int k_scratch_w = 512;    /**< Max sprite width we expect; grown if needed. */
    uint8_t* m_scratch = nullptr;          /**< Width × Height × 2 bytes. */
    int      m_scratch_capacity = 0;       /**< Allocated byte count. */

    /* --- Internal helpers --- */

    /**  Ensure m_scratch is at least w*h*2 bytes. */
    bool EnsureScratch(int w, int h);

    /** Decode one TbSprite's RLE data to [index, alpha] pairs in m_scratch.
     *  Transparent pixels → [0, 0]; opaque pixels → [palette_index, 255].
     *  @return false if sprite data is null or dimensions are zero. */
    bool StoreSprite(const TbSprite* spr);

    /** Allocate a region of @p w × @p h texels and upload from m_scratch.
     *  Advances the shelf packer cursor.
     *  @param out  Filled with the UV rect and page index on success. */
    bool PackAndUpload(int w, int h, AtlasEntry* out);

    /** Open a new atlas page (allocates GL texture if needed). */
    bool AllocPage();
};

#endif // PLATFORM_VITA
#endif // UI_SPRITE_ATLAS_H
