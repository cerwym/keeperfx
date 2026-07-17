/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxsprAtlas.hpp
 *     Viewer-side texture atlas for an .fxspr sprite sheet.
 * @par Purpose:
 *     Packs every non-empty sprite of a loaded kfx::FxSprSheet into a small
 *     number of RGBA8 GL "page" textures using a height-sorted shelf packer,
 *     so the ImGui debug viewer can draw thousands of sprites (e.g. the ~9k
 *     creature frames) as UV sub-rects of a handful of textures instead of
 *     creating one GL texture per sprite.
 *
 *     This is deliberately independent of the game's GLSpriteAtlas (which is
 *     welded to TbSprite/SpriteHandle); it operates purely on the already
 *     final RGBA8 pixels exposed by FxSprSheet::sprite(). It also doubles as
 *     the substrate for later runtime consumption from .fxspr.
 */
/******************************************************************************/
#pragma once

#ifdef KEEPERFX_IMGUI_ENABLED
#ifdef RENDERER_OPENGL_ENABLED

#include <glad/glad.h>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kfx {

class FxSprSheet;

/** One packed sprite region within a page. UVs are normalised [0,1]. */
struct FxsprAtlasRegion {
    int   page = -1;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    int   w = 0, h = 0;
};

/** Shelf-packed RGBA8 atlas built from a single FxSprSheet. One instance owns
 *  its GL page textures; build() replaces any previous contents. */
class FxsprAtlas {
public:
    FxsprAtlas() = default;
    ~FxsprAtlas() { free(); }

    FxsprAtlas(const FxsprAtlas&)            = delete;
    FxsprAtlas& operator=(const FxsprAtlas&) = delete;

    /** Pack + upload every non-empty sprite of `sheet`. Returns true if at
     *  least one page was created. Must run on the GL/render thread. Frees any
     *  previous contents first. */
    bool build(const FxSprSheet& sheet);

    /** Delete all page textures and clear state. Safe to call repeatedly. */
    void free();

    bool built() const { return m_built; }

    /** GL texture id for page `i`, or 0 if out of range. */
    GLuint page(int i) const;
    int    pageCount() const { return (int)m_pages.size(); }
    int    pageDim()   const { return m_page_dim; }

    /** Fill `out` for sprite entry `index`. Returns false for empty / skipped
     *  entries (or before build). */
    bool region(int index, FxsprAtlasRegion& out) const;

    /** Total VRAM occupied by the page textures, in bytes. */
    std::size_t vramBytes() const {
        return (std::size_t)m_pages.size() * (std::size_t)m_page_dim
             * (std::size_t)m_page_dim * 4u;
    }

private:
    std::vector<GLuint>                        m_pages;
    std::unordered_map<int, FxsprAtlasRegion>  m_regions;
    int  m_page_dim = 0;
    bool m_built    = false;
};

} // namespace kfx

#endif // RENDERER_OPENGL_ENABLED
#endif // KEEPERFX_IMGUI_ENABLED
