/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file UISpriteAtlas.cpp
 *     GPU texture atlas for UI sprites — implementation.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "renderer/vita/UISpriteAtlas.h"

#ifdef PLATFORM_VITA

#include <stdlib.h>
#include <string.h>
#include "globals.h"
#include "bflib_sprite.h"
#include "post_inc.h"

// TbSpriteSheet is opaque — accessed only through the public API:
//   get_sprite(sheet, idx)  and  num_sprites(sheet)  from bflib_sprite.h.

// ---------------------------------------------------------------------------
// Key: the sprite's Data pointer — unique per sprite, stable for sheet lifetime.
// ---------------------------------------------------------------------------
static inline uintptr_t sprite_key(const TbSprite* spr)
{
    return (uintptr_t)spr->Data;
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::EnsureScratch(int w, int h)
{
    // Validate sprite dimensions before calculating size (prevent integer overflow)
    // Max reasonable sprite size: 4096×4096 (typical GPU texture limit)
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        ERRORLOG("UISpriteAtlas: invalid scratch dimensions w=%d h=%d", w, h);
        return false;
    }
    
    // Use int64_t to prevent signed integer overflow: w * h * 2
    // Example overflow: 65535 * 65535 * 2 = 0x10000FDFE, truncates to garbage when cast to int32
    int64_t need64 = (int64_t)w * (int64_t)h * 2;
    
    // Enforce max 16MB scratch buffer (typical for single sprite decoding)
    const int64_t MAX_SCRATCH = 16 * 1024 * 1024;
    if (need64 > MAX_SCRATCH) {
        ERRORLOG("UISpriteAtlas: scratch size too large (%lld bytes, max %lld)", need64, MAX_SCRATCH);
        return false;
    }
    
    int need = (int)need64;  // Safe cast: we've verified need64 fits in int
    
    if (need <= m_scratch_capacity) return true;
    uint8_t* nb = (uint8_t*)realloc(m_scratch, need);
    if (!nb) {
        ERRORLOG("UISpriteAtlas: scratch alloc failed (%d bytes)", need);
        return false;
    }
    m_scratch = nb;
    m_scratch_capacity = need;
    return true;
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::StoreSprite(const TbSprite* spr)
{
    if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0)
        return false;

    const int w = spr->SWidth;
    const int h = spr->SHeight;
    if (!EnsureScratch(w, h)) return false;

    uint8_t* dst = m_scratch;  // 2 bytes per pixel: [index, alpha]
    const uint8_t* sp = spr->Data;

    for (int row = 0; row < h; row++)
    {
        int  col     = 0;
        bool hit_eol = false;

        while (col < w)
        {
            int8_t cmd = (int8_t)(*sp++);
            if (cmd == 0)
            {
                // End of line — fill remaining pixels with transparent.
                while (col < w) {
                    uint8_t* px = dst + (row * w + col) * 2;
                    px[0] = 0; px[1] = 0;
                    col++;
                }
                hit_eol = true;
                break;
            }
            else if (cmd < 0)
            {
                // Transparent run: -cmd pixels
                int count = -cmd;
                while (count-- > 0 && col < w) {
                    uint8_t* px = dst + (row * w + col) * 2;
                    px[0] = 0; px[1] = 0;
                    col++;
                }
            }
            else
            {
                // Opaque run: cmd pixels follow; store raw palette index.
                int count = cmd;
                while (count-- > 0 && col < w)
                {
                    uint8_t idx = *sp++;
                    uint8_t* px = dst + (row * w + col) * 2;
                    px[0] = idx;   // palette index
                    px[1] = 0xFF;  // fully opaque
                    col++;
                }
                // When col>=w caused early loop exit, `count--` ran one extra
                // time without consuming a byte, so we need count+1 skips.
                // Natural completion (all pixels drawn) leaves count==-1.
                if (count >= 0)
                    sp += count + 1;  // skip remaining pixel bytes
            }
        }

        if (!hit_eol) {
            while (true) {
                int8_t skip = (int8_t)(*sp++);
                if (skip == 0) break;
                if (skip > 0) sp += skip;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::AllocPage()
{
    if (m_page_count >= k_atlas_max_pages) {
        ERRORLOG("UISpriteAtlas: atlas page limit (%d) reached", k_atlas_max_pages);
        return false;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Validate page size (2048×2048 is a fixed constant)
    if (k_atlas_page_size <= 0 || k_atlas_page_size > 4096) {
        ERRORLOG("UISpriteAtlas: invalid page size %d (must be 1-4096)", k_atlas_page_size);
        glDeleteTextures(1, &tex);
        return false;
    }
    // Allocate storage: zero-filled (all transparent, index=0, alpha=0).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                 k_atlas_page_size, k_atlas_page_size, 0,
                 GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, nullptr);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        ERRORLOG("UISpriteAtlas: glTexImage2D failed (page %d): 0x%x", m_page_count, err);
        glDeleteTextures(1, &tex);
        return false;
    }
    m_textures[m_page_count++] = tex;
    m_cur_page  = m_page_count - 1;
    m_shelf_x   = 0;
    m_shelf_y   = 0;
    m_shelf_h   = 0;
    SYNCDBG(7, "UISpriteAtlas: allocated page %d (tex %u)", m_cur_page, tex);
    return true;
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::PackAndUpload(int w, int h, AtlasEntry* out)
{
    const int padded_w = w + k_atlas_padding;
    const int padded_h = h + k_atlas_padding;

    // Does the sprite fit on the current shelf?
    if (m_shelf_x + padded_w > k_atlas_page_size)
    {
        // Start a new shelf on the current page.
        m_shelf_y += m_shelf_h + k_atlas_padding;
        m_shelf_x  = 0;
        m_shelf_h  = 0;
    }

    // Does the new shelf fit on the current page?
    if (m_shelf_y + padded_h > k_atlas_page_size)
    {
        // Open a new page.
        if (!AllocPage()) return false;
    }

    // Compute UV.
    const float page_f = (float)k_atlas_page_size;
    out->u0   = (float)(m_shelf_x)         / page_f;
    out->v0   = (float)(m_shelf_y)         / page_f;
    out->u1   = (float)(m_shelf_x + w)     / page_f;
    out->v1   = (float)(m_shelf_y + h)     / page_f;
    out->page = m_cur_page;
    out->valid = true;

    // Upload palette-indexed pixels [index, alpha] to the atlas texture.
    glBindTexture(GL_TEXTURE_2D, m_textures[m_cur_page]);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    m_shelf_x, m_shelf_y, w, h,
                    GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                    m_scratch);

    m_shelf_x += padded_w;
    if (padded_h > m_shelf_h) m_shelf_h = padded_h;

    return true;
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::Init()
{
    if (m_initialized) return true;
    if (!AllocPage()) return false;
    m_initialized = true;
    SYNCLOG("UISpriteAtlas: initialized (page size %d)", k_atlas_page_size);
    return true;
}

// ---------------------------------------------------------------------------
void UISpriteAtlas::AddSheet(const TbSpriteSheet* sheet)
{
    if (!m_initialized || !sheet) return;

    long count = num_sprites(sheet);
    int packed = 0;
    int skipped = 0;

    for (long i = 0; i < count; i++)
    {
        const TbSprite* spr = get_sprite(sheet, i);
        if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0) {
            skipped++;
            continue;
        }

        uintptr_t key = sprite_key(spr);
        // Only skip if the existing entry is still valid (live sheet).
        // An entry that exists but has valid=false belongs to a freed sheet;
        // the allocator may have reused that Data pointer for this new sprite,
        // so we must re-pack it.
        auto existing = m_entries.find(key);
        if (existing != m_entries.end() && existing->second.valid) continue;

        if (!StoreSprite(spr)) {
            skipped++;
            continue;
        }
        AtlasEntry entry = {};
        if (!PackAndUpload(spr->SWidth, spr->SHeight, &entry)) {
            ERRORLOG("UISpriteAtlas: atlas full — sprite %ld not packed", i);
            break;
        }

        m_entries[key] = entry;
        m_sheet_keys[(uintptr_t)sheet].push_back(key);
        packed++;
    }

    SYNCDBG(7, "UISpriteAtlas: sheet %p — packed %d sprites, skipped %d",
            (const void*)sheet, packed, skipped);
}

// ---------------------------------------------------------------------------
void UISpriteAtlas::RemoveSheet(const TbSpriteSheet* sheet)
{
    if (!sheet) return;
    auto it = m_sheet_keys.find((uintptr_t)sheet);
    if (it == m_sheet_keys.end()) return;
    for (uintptr_t key : it->second) {
        auto eit = m_entries.find(key);
        if (eit != m_entries.end())
            eit->second.valid = false;
    }
    m_sheet_keys.erase(it);
}

// ---------------------------------------------------------------------------
bool UISpriteAtlas::GetUV(const TbSprite* spr, AtlasEntry* out) const
{
    if (!spr || !spr->Data) return false;
    auto it = m_entries.find(sprite_key(spr));
    if (it == m_entries.end() || !it->second.valid) return false;
    *out = it->second;
    return true;
}

// ---------------------------------------------------------------------------
GLuint UISpriteAtlas::GetTexture(int page) const
{
    if (page < 0 || page >= m_page_count) return 0;
    return m_textures[page];
}

// ---------------------------------------------------------------------------
void UISpriteAtlas::Free()
{
    m_entries.clear();
    for (int i = 0; i < m_page_count; i++) {
        if (m_textures[i]) { glDeleteTextures(1, &m_textures[i]); m_textures[i] = 0; }
    }
    m_page_count = 0;
    m_cur_page   = 0;
    m_shelf_x    = 0;
    m_shelf_y    = 0;
    m_shelf_h    = 0;
    if (m_scratch) { free(m_scratch); m_scratch = nullptr; }
    m_scratch_capacity = 0;
    m_initialized = false;
}

#endif // PLATFORM_VITA
