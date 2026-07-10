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
#include "renderer/RendererSettings.h"
#include "renderer/RendererManager.h"
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

/******************************************************************************/

namespace {
// Materialise one row-segment of palette indices into RGBA8, mirroring the
// indexed sprite shader: index 0 -> fully transparent; otherwise the VGA6
// palette entry expanded to 8-bit (<<2) with opaque alpha.
inline void materialise_row(uint8_t* dst_rgba, const uint8_t* src_idx, int count,
                            const unsigned char* pal /*768 VGA6*/)
{
    for (int x = 0; x < count; ++x) {
        const uint8_t idx = src_idx[x];
        if (idx == 0) {
            dst_rgba[0] = dst_rgba[1] = dst_rgba[2] = dst_rgba[3] = 0;
        } else {
            dst_rgba[0] = (uint8_t)(pal[idx * 3 + 0] << 2);
            dst_rgba[1] = (uint8_t)(pal[idx * 3 + 1] << 2);
            dst_rgba[2] = (uint8_t)(pal[idx * 3 + 2] << 2);
            dst_rgba[3] = 255;
        }
        dst_rgba += 4;
    }
}
} // namespace

/******************************************************************************/

bool GLSpriteAtlas::Init()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    return Init_Internal();
}

bool GLSpriteAtlas::Init_Internal()
{
    // CPU-only: reset pixel buffer and packer state.  GL texture creation is
    // deferred to FlushPendingGL() so it can run on the render thread that
    // owns the GL context after the first EndFrame().
    m_pixels.assign((size_t)k_atlas_w * k_atlas_h, 0u);

    // Decide truecolour mode for this atlas generation and snapshot the palette
    // it will be materialised against.  Baking the palette here means the RGBA
    // atlas is rebuilt whenever palette_mode changes (RendererApplySettings
    // schedules a rebuild), which is the intended lifecycle.
    m_rgba_enabled = (g_renderer_settings.palette_mode == RENDERER_PALETTE_TRUECOLOUR);
    if (m_rgba_enabled) {
        const unsigned char* pal = RendererGetActivePalette();
        if (pal) memcpy(m_pal_snapshot, pal, sizeof(m_pal_snapshot));
        else     memset(m_pal_snapshot, 0, sizeof(m_pal_snapshot));
        m_rgba.assign((size_t)k_atlas_w * k_atlas_h * 4, 0u);
    } else {
        m_rgba.clear();
    }

    m_cursor_x    = 1; // reserve atlas texel (0,0) as a safe fallback UV
    m_shelf_y     = 0;
    m_shelf_h     = 0;
    m_dirty_y_min = k_atlas_h;
    m_dirty_y_max = -1;
    m_gl_init_needed = true;  // signal FlushPendingGL() to create the GL texture

    SYNCLOG("GLSpriteAtlas: CPU init done (%s) — GL texture deferred to render thread",
            m_rgba_enabled ? "truecolour RGBA8" : "indexed R8");
    return true;
}

void GLSpriteAtlas::Free()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    Free_Internal();
}

void GLSpriteAtlas::Free_Internal()
{
    // Save the old texture ID so FlushPendingGL() can delete it on the render
    // thread.  Calling glDeleteTextures here would fail without a GL context.
    if (m_texture) {
        m_old_texture = m_texture;
        m_texture = 0;
    }
    if (m_rgba_texture) {
        m_old_rgba_texture = m_rgba_texture;
        m_rgba_texture = 0;
    }
    m_gl_init_needed = false;
    m_pixels.clear();
    m_rgba.clear();
    m_sprite_to_handle.clear();
    m_handle_uvs.clear();
    m_next_handle = 0;
}

/******************************************************************************/


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
    {
        KFX_ZONE("SpriteAtlas::DecodeRLE");
        LbSpriteDecode(dst, k_atlas_w, spr);
    }

    // Mirror the just-decoded region into the RGBA atlas (truecolour mode).
    if (m_rgba_enabled && !m_rgba.empty()) {
        for (int y = 0; y < h; ++y) {
            const uint8_t* src_row = m_pixels.data()
                                   + (size_t)(m_shelf_y + y) * k_atlas_w + m_cursor_x;
            uint8_t* dst_row = m_rgba.data()
                             + (((size_t)(m_shelf_y + y) * k_atlas_w + m_cursor_x) * 4);
            materialise_row(dst_row, src_row, w, m_pal_snapshot);
        }
    }

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

    if (m_rgba_enabled && m_rgba_texture && !m_rgba.empty()) {
        glBindTexture(GL_TEXTURE_2D, m_rgba_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, m_dirty_y_min,
                        k_atlas_w, h,
                        GL_RGBA, GL_UNSIGNED_BYTE,
                        m_rgba.data() + (size_t)m_dirty_y_min * k_atlas_w * 4);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    m_dirty_y_min = k_atlas_h;
    m_dirty_y_max = -1;
}

/******************************************************************************/
void GLSpriteAtlas::FlushPendingGL()
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    // Delete the previous texture that was freed on the game thread.
    if (m_old_texture) {
        glDeleteTextures(1, &m_old_texture);
        m_old_texture = 0;
    }
    if (m_old_rgba_texture) {
        glDeleteTextures(1, &m_old_rgba_texture);
        m_old_rgba_texture = 0;
    }

    if (m_gl_init_needed && !m_pixels.empty())
    {
        // Full (re)create: all sheets have already been packed into m_pixels by
        // AddSheet() on the game thread.  Upload the whole buffer in one call.
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

        // Truecolour: create + upload the parallel RGBA8 atlas.
        if (m_rgba_enabled && !m_rgba.empty()) {
            glGenTextures(1, &m_rgba_texture);
            KFX_GL_LABEL(GL_TEXTURE, m_rgba_texture, "SpriteAtlas/TexRGBA");
            glBindTexture(GL_TEXTURE_2D, m_rgba_texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, k_atlas_w, k_atlas_h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, m_rgba.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            SYNCLOG("GLSpriteAtlas: initialised %dx%d RGBA8 texture (tex=%u)",
                    k_atlas_w, k_atlas_h, m_rgba_texture);
        }

        m_gl_init_needed = false;
        m_dirty_y_min = k_atlas_h;  // full upload done, reset dirty range
        m_dirty_y_max = -1;
        SYNCLOG("GLSpriteAtlas: initialised %dx%d R8 texture (tex=%u)", k_atlas_w, k_atlas_h, m_texture);
    }
    else
    {
        // Incremental: flush any sub-regions dirtied by AddSheet() calls
        // that ran after the last full init (e.g. pointer/frontend sprites).
        flush_dirty();
    }
}
void GLSpriteAtlas::Rebuild(const struct TbSpriteSheet* const* sheets,
                             const char* const*                 names,
                             int                                count)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    ++m_generation;
    Free_Internal();
    if (!Init_Internal()) {
        ERRORLOG("GLSpriteAtlas::Rebuild: Init_Internal() FAILED");
        return;
    }
    for (int i = 0; i < count; ++i) {
        if (sheets[i]) AddSheet_Internal(sheets[i], names ? names[i] : nullptr);
    }
}

void GLSpriteAtlas::AddSheet(const struct TbSpriteSheet* sheet, const char* name)
{
    if (!sheet) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    AddSheet_Internal(sheet, name);
}

void GLSpriteAtlas::AddSheet_Internal(const struct TbSpriteSheet* sheet, const char* name)
{
    long n = num_sprites(sheet);
    int packed = 0;
    const char* label = name ? name : "(unknown)";
    SYNCLOG("GLSpriteAtlas::AddSheet: Adding sheet '%s' (%p) with %ld sprites", label, sheet, n);

    for (long i = 0; i < n; ++i) {
        const struct TbSprite* spr = get_sprite(sheet, i);
        if (!spr || !spr->Data || spr->SWidth == 0 || spr->SHeight == 0) continue;
        if (m_sprite_to_handle.count(spr)) continue; // already present (sheet re-added)
        if (m_next_handle >= kSpriteHandleInvalidIndex) {
            ERRORLOG("GLSpriteAtlas::AddSheet: sprite handle table exhausted");
            break;
        }
        SpriteUV uv;
        if (pack_sprite(spr, uv)) {
            SpriteHandle h = MakeSpriteHandle(m_generation, (uint16_t)m_next_handle++);
            m_sprite_to_handle[spr] = h;
            m_handle_uvs.push_back(uv);
            ++packed;
        }
    }
    // Do NOT call flush_dirty() here — AddSheet() may be called from the game
    // thread (without a GL context) during level load.  FlushPendingGL() will
    // do the actual upload (glTexImage2D or glTexSubImage2D) on the render thread.
    SYNCLOG("GLSpriteAtlas::AddSheet: packed %d/%ld sprites from sheet '%s' (%p) (total handles: %d)",
           packed, n, label, sheet, (int)m_handle_uvs.size());
}

void GLSpriteAtlas::RemoveSheet(const struct TbSpriteSheet* sheet)
{
    if (!sheet) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
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
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetHandle_Unlocked(spr);
}

SpriteHandle GLSpriteAtlas::GetHandle_Unlocked(const struct TbSprite* spr) const
{
    auto it = m_sprite_to_handle.find(spr);
    return (it != m_sprite_to_handle.end()) ? it->second : kInvalidSpriteHandle;
}

bool GLSpriteAtlas::GetUV(SpriteHandle h, SpriteUV& out) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetUV_Unlocked(h, out);
}

bool GLSpriteAtlas::GetUV_Unlocked(SpriteHandle h, SpriteUV& out) const
{
    if (h == kInvalidSpriteHandle)
        return false;

    const uint16_t generation = SpriteHandleGeneration(h);
    const uint16_t index      = SpriteHandleIndex(h);
    if (generation != m_generation || index >= m_handle_uvs.size())
        return false;

    out = m_handle_uvs[index];
    return true;
}

bool GLSpriteAtlas::GetUV(const struct TbSprite* spr, SpriteUV& out) const
{
    // Take a single shared lock for the combined handle-lookup + UV-lookup.
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return GetUV_Unlocked(GetHandle_Unlocked(spr), out);
}

GLuint GLSpriteAtlas::GetTexture() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_texture;
}

GLuint GLSpriteAtlas::GetRGBATexture() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_rgba_texture;
}

bool GLSpriteAtlas::IsRGBAEnabled() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_rgba_enabled;
}

void GLSpriteAtlas::RefreshRGBAForPalette(const unsigned char* pal)
{
    if (!pal) return;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (!m_rgba_enabled || m_rgba.empty()) return;
    // A full (re)build is already queued — it materialises fresh in pack_sprite,
    // so skip the incremental re-bake to avoid doing the work twice.
    if (m_gl_init_needed) return;
    // Fast path: palette unchanged since the last bake.
    if (memcmp(pal, m_pal_snapshot, sizeof(m_pal_snapshot)) == 0) return;
    memcpy(m_pal_snapshot, pal, sizeof(m_pal_snapshot));

    // Re-materialise every packed row from the R8 index atlas through the new
    // palette. Unused gaps hold index 0 and stay transparent.
    const int used_h = m_shelf_y + m_shelf_h;
    if (used_h <= 0) return;
    for (int y = 0; y < used_h; ++y) {
        const uint8_t* src = m_pixels.data() + (size_t)y * k_atlas_w;
        uint8_t*       dst = m_rgba.data()   + (size_t)y * k_atlas_w * 4;
        materialise_row(dst, src, k_atlas_w, m_pal_snapshot);
    }
    if (m_dirty_y_min > 0)      m_dirty_y_min = 0;
    if (m_dirty_y_max < used_h) m_dirty_y_max = used_h;
}

size_t GLSpriteAtlas::GetRegisteredCount() const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_sprite_to_handle.size();
}

void GLSpriteAtlas::Snapshot(AtlasSnapshot& out) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    out.width  = k_atlas_w;
    out.height = k_atlas_h;
    out.pixels = m_pixels;  // detached copy of the R8 buffer
    out.entries.clear();
    out.entries.reserve(m_handle_uvs.size());
    // m_handle_uvs is indexed by handle index; every packed sprite lives here.
    // All live handles share the current generation (bumped on each Rebuild).
    for (size_t i = 0; i < m_handle_uvs.size(); ++i) {
        AtlasEntry e;
        e.handle = MakeSpriteHandle(m_generation, (uint16_t)i);
        e.uv     = m_handle_uvs[i];
        out.entries.push_back(e);
    }
}

uint8_t* GLSpriteAtlas::GetSpriteMask(SpriteHandle h, int* out_w, int* out_h, int* out_stride) const
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    SpriteUV uv;
    if (!GetUV_Unlocked(h, uv)) return nullptr;

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
