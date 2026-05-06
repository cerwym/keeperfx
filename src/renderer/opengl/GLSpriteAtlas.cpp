/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLSpriteAtlas.cpp
 *     Desktop OpenGL sprite texture atlas — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLSpriteAtlas.h"

#ifdef RENDERER_OPENGL_ENABLED

#include <cstring>
#include <cstdlib>
#include "bflib_basics.h"
#include "bflib_sprite.h"
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

/******************************************************************************/

bool GLSpriteAtlas::Init()
{
    m_pixels.assign((size_t)k_atlas_w * k_atlas_h, 0u);

    glGenTextures(1, &m_texture);
    KFX_GL_LABEL(GL_TEXTURE, m_texture, "SpriteAtlas/Tex");
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, k_atlas_w, k_atlas_h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, m_pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    m_cursor_x   = 0;
    m_shelf_y    = 0;
    m_shelf_h    = 0;
    m_dirty_y_min = k_atlas_h;
    m_dirty_y_max = -1;

    SYNCLOG("GLSpriteAtlas: initialised %dx%d R8 texture", k_atlas_w, k_atlas_h);
    return true;
}

void GLSpriteAtlas::Free()
{
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    m_pixels.clear();
    m_sprite_to_handle.clear();
    m_handle_uvs.clear();
    m_next_handle = 0;
}

/******************************************************************************/

void GLSpriteAtlas::decode_rle(uint8_t* dst, int dst_stride, const struct TbSprite* spr)
{
    KFX_ZONE("SpriteAtlas::DecodeRLE");
    // Zero out the sprite area (index 0 == transparent)
    for (int y = 0; y < spr->SHeight; ++y)
        memset(dst + y * dst_stride, 0, spr->SWidth);

    const signed char* sp = reinterpret_cast<const signed char*>(spr->Data);
    int total_pixels_written = 0;

    for (int y = 0; y < spr->SHeight; ++y) {
        uint8_t* row = dst + y * dst_stride;
        int x = 0;
        while (true) {
            signed char cmd = *sp++;
            if (cmd == 0) break;            // end of row
            if (cmd < 0) {
                x += (int)(-cmd);           // transparent run — skip pixels
            } else {
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
                    if (x < spr->SWidth) {
                        row[x] = (uint8_t)(*sp);
                        if (*sp != 0) total_pixels_written++;
                    }
                    ++sp;
                    ++x;
                }
            }
        }
    }

    // Log sprites that decode to all zeros (potential black squares)
    if (total_pixels_written == 0) {
        static int empty_sprite_count = 0;
        if (empty_sprite_count < 10) {
            WARNLOG("GLSpriteAtlas: Sprite %dx%d decoded to all zeros (palette index 0) - will appear as transparent", 
                   spr->SWidth, spr->SHeight);
            // Show first few bytes of sprite data for debugging
            const unsigned char* raw = spr->Data;
            SYNCLOG("GLSpriteAtlas: Empty sprite raw data: %02X %02X %02X %02X %02X %02X...", 
                   raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
            empty_sprite_count++;
        }
    } else {
        static int decoded_sprite_count = 0;
        if (decoded_sprite_count < 5) {
            SYNCLOG("GLSpriteAtlas: Sprite %dx%d decoded successfully: %d non-zero pixels", 
                   spr->SWidth, spr->SHeight, total_pixels_written);
            decoded_sprite_count++;
        }
    }
}

bool GLSpriteAtlas::pack_sprite(const struct TbSprite* spr, SpriteUV& out)
{
    const int w = spr->SWidth;
    const int h = spr->SHeight;
    if (w <= 0 || h <= 0) return false;

    const int alloc_w = w + 1; // 1-pixel margin
    const int alloc_h = h + 1;

    // Advance to next shelf if the sprite doesn't fit horizontally
    if (m_cursor_x + alloc_w > k_atlas_w) {
        m_shelf_y += m_shelf_h;
        m_cursor_x = 0;
        m_shelf_h  = 0;
    }

    if (m_shelf_y + alloc_h > k_atlas_h) {
        ERRORLOG("GLSpriteAtlas: atlas full — cannot pack %dx%d sprite", w, h);
        return false;
    }

    if (alloc_h > m_shelf_h) m_shelf_h = alloc_h;

    // Decode RLE into the CPU pixel buffer
    uint8_t* dst = m_pixels.data() + m_shelf_y * k_atlas_w + m_cursor_x;
    decode_rle(dst, k_atlas_w, spr);

    // UV in normalised [0,1] atlas space
    out.u0 = (float) m_cursor_x       / (float)k_atlas_w;
    out.v0 = (float) m_shelf_y        / (float)k_atlas_h;
    out.u1 = (float)(m_cursor_x + w)  / (float)k_atlas_w;
    out.v1 = (float)(m_shelf_y  + h)  / (float)k_atlas_h;
    out.pixel_w = (uint16_t)w;
    out.pixel_h = (uint16_t)h;

    // Expand dirty region for the upload in flush_dirty()
    if (m_shelf_y < m_dirty_y_min)     m_dirty_y_min = m_shelf_y;
    if (m_shelf_y + h > m_dirty_y_max) m_dirty_y_max = m_shelf_y + h;

    m_cursor_x += alloc_w;
    return true;
}

void GLSpriteAtlas::flush_dirty()
{
    KFX_ZONE("SpriteAtlas::FlushDirty");
    if (m_dirty_y_min > m_dirty_y_max) return;
    int h = m_dirty_y_max - m_dirty_y_min;
    if (h <= 0) return;

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    0, m_dirty_y_min,
                    k_atlas_w, h,
                    GL_RED, GL_UNSIGNED_BYTE,
                    m_pixels.data() + (size_t)m_dirty_y_min * k_atlas_w);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_dirty_y_min = k_atlas_h;
    m_dirty_y_max = -1;
}

/******************************************************************************/

void GLSpriteAtlas::AddSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    long n = num_sprites(sheet);
    int packed = 0;
    SYNCLOG("GLSpriteAtlas::AddSheet: Adding sheet %p with %ld sprites", sheet, n);

    for (long i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0) continue;
        if (m_sprite_to_handle.count(spr)) continue; // already present (sheet re-added)
        SpriteUV uv;
        if (pack_sprite(spr, uv)) {
            SpriteHandle h = m_next_handle++;
            m_sprite_to_handle[spr] = h;
            m_handle_uvs.push_back(uv);
            ++packed;
        }
    }
    flush_dirty();
    SYNCLOG("GLSpriteAtlas::AddSheet: packed %d/%ld sprites from sheet %p (total handles: %d)", 
           packed, n, sheet, (int)m_handle_uvs.size());
}

void GLSpriteAtlas::RemoveSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    long n = num_sprites(sheet);
    SYNCLOG("GLSpriteAtlas::RemoveSheet: Removing sheet %p with %ld sprites", sheet, n);

    for (long i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (spr) m_sprite_to_handle.erase(spr);
    }
    SYNCLOG("GLSpriteAtlas::RemoveSheet: Removed sheet %p (remaining handles: %d)", 
           sheet, (int)m_handle_uvs.size());
    // Note: atlas pixels and handle slots not reclaimed; space is lost until full reinit.
}

SpriteHandle GLSpriteAtlas::GetHandle(const struct TbSprite* spr) const
{
    auto it = m_sprite_to_handle.find(spr);
    return (it != m_sprite_to_handle.end()) ? it->second : kInvalidSpriteHandle;
}

bool GLSpriteAtlas::GetUV(SpriteHandle h, SpriteUV& out) const
{
    if (h >= (SpriteHandle)m_handle_uvs.size()) return false;
    out = m_handle_uvs[h];
    return true;
}

bool GLSpriteAtlas::GetUV(const struct TbSprite* spr, SpriteUV& out) const
{
    return GetUV(GetHandle(spr), out);
}

uint8_t* GLSpriteAtlas::GetSpriteMask(SpriteHandle h, int* out_w, int* out_h, int* out_stride) const
{
    SpriteUV uv;
    if (!GetUV(h, uv)) return nullptr;

    const int w = uv.pixel_w;
    const int h_px = uv.pixel_h;
    if (w <= 0 || h_px <= 0) return nullptr;

    // Compute the top-left pixel in the atlas from UV coordinates
    const int atlas_x = (int)(uv.u0 * (float)k_atlas_w + 0.5f);
    const int atlas_y = (int)(uv.v0 * (float)k_atlas_h + 0.5f);

    // Stride in bytes: one bit per pixel, rounded up to whole bytes
    const int stride = (w + 7) / 8;
    uint8_t* mask = (uint8_t*)calloc((size_t)stride * h_px, 1);
    if (!mask) return nullptr;

    for (int y = 0; y < h_px; ++y) {
        const uint8_t* src_row = m_pixels.data() + (size_t)(atlas_y + y) * k_atlas_w + atlas_x;
        for (int x = 0; x < w; ++x) {
            if (src_row[x] != 0) {
                mask[y * stride + (x >> 3)] |= (uint8_t)(1u << (x & 7));
            }
        }
    }

    *out_w      = w;
    *out_h      = h_px;
    *out_stride = stride;
    return mask;
}

#endif // RENDERER_OPENGL_ENABLED
