/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxsprAtlas.cpp
 *     Viewer-side texture atlas for an .fxspr sprite sheet — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/FxsprAtlas.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED
#ifdef RENDERER_OPENGL_ENABLED

#include "kfx/assets/FxSprSheet.h"

#include <algorithm>
#include <cstring>

#include "post_inc.h"

namespace kfx {

namespace {

constexpr int kPad         = 1;     // transparent gutter between packed sprites
constexpr int kMaxPageDim  = 4096;  // cap even if the GL max is larger

int query_page_dim()
{
    GLint gl_max = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max);
    int dim = (gl_max > 0) ? (int)gl_max : 2048;
    if (dim > kMaxPageDim) dim = kMaxPageDim;
    if (dim < 256)         dim = 256;
    return dim;
}

// Blit a top-to-bottom RGBA8 sprite into an RGBA8 page buffer at (dx,dy).
void blit_rgba(std::vector<uint8_t>& dst, int page_dim, int dx, int dy,
               const uint8_t* src, int w, int h)
{
    const size_t dst_stride = (size_t)page_dim * 4u;
    const size_t src_stride = (size_t)w * 4u;
    for (int row = 0; row < h; ++row) {
        uint8_t*       d = dst.data() + (size_t)(dy + row) * dst_stride + (size_t)dx * 4u;
        const uint8_t* s = src + (size_t)row * src_stride;
        std::memcpy(d, s, src_stride);
    }
}

GLuint upload_page(const std::vector<uint8_t>& pixels, int page_dim)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, page_dim, page_dim, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

} // namespace

void FxsprAtlas::free()
{
    for (GLuint tex : m_pages)
        if (tex) glDeleteTextures(1, &tex);
    m_pages.clear();
    m_regions.clear();
    m_page_dim = 0;
    m_built    = false;
}

GLuint FxsprAtlas::page(int i) const
{
    if (i < 0 || i >= (int)m_pages.size())
        return 0;
    return m_pages[i];
}

bool FxsprAtlas::region(int index, FxsprAtlasRegion& out) const
{
    auto it = m_regions.find(index);
    if (it == m_regions.end())
        return false;
    out = it->second;
    return true;
}

bool FxsprAtlas::build(const FxSprSheet& sheet)
{
    free();

    const int page_dim = query_page_dim();
    m_page_dim = page_dim;

    // Collect drawable entries: index + dimensions. Skip empties and anything
    // that cannot fit on a page (creature frames top out at 256, so this only
    // guards against pathological data).
    struct Item { int index; int w; int h; };
    std::vector<Item> items;
    const int n = sheet.count();
    items.reserve((size_t)(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) {
        FxSprSprite spr;
        if (!sheet.sprite(i, spr))
            continue;
        if (spr.width == 0 || spr.height == 0 || spr.rgba == nullptr)
            continue;
        if (spr.width + kPad > page_dim || spr.height + kPad > page_dim)
            continue;
        items.push_back(Item{ i, (int)spr.width, (int)spr.height });
    }

    if (items.empty()) {
        m_built = true; // built, just nothing to draw
        return false;
    }

    // Height-descending gives tight shelves. Keep original entry index as the
    // region key so draw order is independent of pack order.
    std::vector<int> order(items.size());
    for (size_t k = 0; k < items.size(); ++k) order[k] = (int)k;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return items[a].h > items[b].h; });

    std::vector<uint8_t> page(( size_t)page_dim * (size_t)page_dim * 4u, 0);
    int cursor_x = 0;
    int shelf_y  = 0;
    int shelf_h  = 0;
    const float inv = 1.0f / (float)page_dim;

    auto flush_page = [&]() {
        m_pages.push_back(upload_page(page, page_dim));
        std::fill(page.begin(), page.end(), (uint8_t)0);
    };

    for (int oi : order) {
        const Item& it = items[(size_t)oi];

        // Advance to a new shelf when the current row is full.
        if (cursor_x + it.w + kPad > page_dim) {
            cursor_x = 0;
            shelf_y += shelf_h + kPad;
            shelf_h  = 0;
        }
        // Advance to a new page when the current shelf overflows the page.
        if (shelf_y + it.h + kPad > page_dim) {
            flush_page();
            cursor_x = 0;
            shelf_y  = 0;
            shelf_h  = 0;
        }

        FxSprSprite spr;
        if (!sheet.sprite(it.index, spr) || spr.rgba == nullptr)
            continue;

        const int px = cursor_x;
        const int py = shelf_y;
        blit_rgba(page, page_dim, px, py, spr.rgba, it.w, it.h);

        FxsprAtlasRegion r;
        r.page = (int)m_pages.size();      // page currently being filled
        r.u0   = (float)px * inv;
        r.v0   = (float)py * inv;
        r.u1   = (float)(px + it.w) * inv;
        r.v1   = (float)(py + it.h) * inv;
        r.w    = it.w;
        r.h    = it.h;
        m_regions[it.index] = r;

        cursor_x += it.w + kPad;
        if (it.h > shelf_h) shelf_h = it.h;
    }

    // Upload the final (partly filled) page.
    flush_page();

    m_built = true;
    return !m_pages.empty();
}

} // namespace kfx

#endif // RENDERER_OPENGL_ENABLED
#endif // KEEPERFX_IMGUI_ENABLED
