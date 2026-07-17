/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprSheet.h
 *     In-memory reader for the `.fxspr` truecolour sprite container (v1 + v2).
 *
 *     Renderer- and engine-agnostic: it owns the raw file bytes (and, for v2,
 *     the inflated payload) and hands out borrowed pointers into the RGBA8
 *     payload plus the per-sprite metadata. No GL, no game globals — this is the
 *     pure data layer that the RGBA atlas (and the debug viewer) sit on.
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

/** Per-sprite semantic metadata (v2). For v1 sheets everything except
 *  width/height defaults (flat, unnamed, category Unknown). `name` is never
 *  null — it is "" when the entry is unnamed. */
struct FxSprMeta {
    uint16_t    width        = 0;
    uint16_t    height       = 0;
    int16_t     offset_x     = 0;
    int16_t     offset_y     = 0;
    int16_t     shadow_offset = 0;
    uint16_t    frame_flags  = 0;
    uint32_t    group_id     = 0;
    uint16_t    frame_index  = 0;
    uint8_t     rotation     = 0;
    uint8_t     view         = 0;      /**< FxSprView */
    uint16_t    category     = 0;      /**< FxSprCategory */
    const char* name         = "";     /**< borrowed; "" when unnamed */
};

/** File-level variant facts (v2). For v1 sheets these default (scale 0,
 *  colour_mode/provenance Unknown). `name` is never null. */
struct FxSprInfo {
    uint16_t    version     = 0;
    uint16_t    kind        = 0;
    uint16_t    scale       = 0;       /**< nominal scale tier; 0 = unspecified */
    uint8_t     colour_mode = 0;       /**< FxSprColourMode */
    uint8_t     provenance  = 0;       /**< FxSprProvenance */
    const char* name        = "";      /**< borrowed display name; "" when none */
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
    int      count() const { return m_valid ? (int)m_entries.size() : 0; }
    uint16_t kind()  const { return m_valid ? m_header.kind : 0; }
    uint16_t flags() const { return m_valid ? m_header.flags : 0; }

    /** File-level variant facts. Valid only while the sheet is loaded. */
    const FxSprInfo& info() const { return m_info; }

    /** Fill `out` for sprite `index`. Returns false if index is out of range.
     *  Empty sprites (the sentinel / zero-size entries) return true with
     *  width==height==0 and rgba==nullptr. */
    bool sprite(int index, FxSprSprite& out) const;

    /** Fill `out` with sprite `index`'s metadata. Returns false if out of range.
     *  Borrowed pointers are valid only while the sheet is loaded. */
    bool meta(int index, FxSprMeta& out) const;

    /** Borrowed display name of sprite `index`, or "" if unnamed / out of range. */
    const char* name(int index) const;

private:
    bool parse();
    bool parseV1();
    bool parseV2();
    /** Resolve a string-table offset to a borrowed C string ("" if invalid). */
    const char* stringAt(uint32_t off) const;

    std::vector<uint8_t>        m_blob;      /**< raw file bytes */
    std::vector<uint8_t>        m_inflated;  /**< inflated payload (v2 compressed); else empty */
    const uint8_t*              m_payload = nullptr; /**< -> payload bytes (inflated or in-blob) */
    size_t                      m_payload_size = 0;
    std::vector<FxSprEntryRich> m_entries;   /**< normalised directory (v1 promoted to rich) */
    std::vector<char>           m_strings;   /**< string table; always begins with '\0' */
    FxSprHeader                 m_header{};
    FxSprInfo                   m_info{};
    bool                        m_valid = false;
};

} // namespace kfx

#endif /* KEEPERFX_KFX_ASSETS_FXSPRSHEET_H */
