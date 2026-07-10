/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprSheet.h
 *     In-memory reader for the `.fxspr` truecolour sprite container (v1).
 *
 *     Renderer- and engine-agnostic: it owns the raw file bytes and hands out
 *     borrowed pointers into the RGBA8 payload. No GL, no game globals — this
 *     is the pure data layer that the RGBA atlas (and the debug viewer) sit on.
 *
 *     Byte layout: src/kfx/assets/FxSprFormat.h.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_ASSETS_FXSPRSHEET_H
#define KEEPERFX_KFX_ASSETS_FXSPRSHEET_H

#include <cstdint>
#include <vector>

#include "kfx/assets/FxSprFormat.h"

namespace kfx {

/** A borrowed view of one sprite's RGBA8 pixels within a loaded sheet. */
struct FxSprSprite {
    uint16_t       width  = 0;
    uint16_t       height = 0;
    const uint8_t* rgba   = nullptr; /**< width*height*4 bytes, top-to-bottom, or null if empty */
};

/** Loads and owns one `.fxspr` file. Move-only. */
class FxSprSheet {
public:
    FxSprSheet() = default;
    FxSprSheet(FxSprSheet&&) noexcept = default;
    FxSprSheet& operator=(FxSprSheet&&) noexcept = default;
    FxSprSheet(const FxSprSheet&) = delete;
    FxSprSheet& operator=(const FxSprSheet&) = delete;

    /** Read + validate a `.fxspr` from an already-resolved filesystem path.
     *  Returns true on success; on failure the sheet is left empty and a
     *  diagnostic is logged. */
    bool loadFromFile(const char* path);

    /** Validate + adopt an in-memory `.fxspr` blob (used by tests / callers
     *  that already have the bytes). Takes ownership of `bytes`. */
    bool loadFromMemory(std::vector<uint8_t>&& bytes);

    bool     valid() const { return m_valid; }
    int      count() const { return m_valid ? (int)m_header.entry_count : 0; }
    uint16_t kind()  const { return m_valid ? m_header.kind : 0; }
    uint16_t flags() const { return m_valid ? m_header.flags : 0; }

    /** Fill `out` for sprite `index`. Returns false if index is out of range.
     *  Empty sprites (the sentinel / zero-size entries) return true with
     *  width==height==0 and rgba==nullptr. */
    bool sprite(int index, FxSprSprite& out) const;

private:
    bool parse();

    std::vector<uint8_t> m_blob;
    FxSprHeader          m_header{};
    bool                 m_valid = false;
};

} // namespace kfx

#endif /* KEEPERFX_KFX_ASSETS_FXSPRSHEET_H */
