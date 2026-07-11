/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file VariantCatalogue.h
 *     Sprite identity + quality-variant catalogue (engine side).
 *
 *     Loads the JSON sprite-pack manifests under FGrp_FxData/spritepacks/*.json
 *     (see tools/fxspr/make_manifest.py + docs/sprite-asset-unification.md) and
 *     answers the questions the comparison viewer and (later) the runtime
 *     hot-swap need:
 *       - given a sheet base + entry index, what is the sprite's stable id, name
 *         and category, and which quality VARIANTS (32/64/128, indexed vs
 *         truecolour, from which pack) exist for it?
 *       - which sprites match a name/id search?
 *
 *     Manifests declare COLLECTIONS (the scale/colour variants of one logical
 *     sheet) plus optional per-entry overrides (names, tags, extra mod-pack
 *     variants). Identities are synthesised on demand as "<base>/<entry>", so a
 *     849-entry sheet costs one Collection, not 849 records.
 *
 *     Pure data + std::string; no GL, no ImGui. The viewer sits on top.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_ASSETS_VARIANTCATALOGUE_H
#define KEEPERFX_KFX_ASSETS_VARIANTCATALOGUE_H

#include <string>
#include <unordered_map>
#include <vector>

namespace kfx {

/** Colour source of a variant (mirrors FxSprColourMode). */
enum class FxColourKind { Indexed, Truecolour };

/** One quality variant of a logical sprite: a specific entry in a specific
 *  .fxspr file (FGrp_StdData-relative path). */
struct SpriteVariant {
    int          scale  = 0;                      /**< nominal tier (32/64/128); 0 = unspecified */
    FxColourKind colour = FxColourKind::Indexed;
    std::string  file;                            /**< e.g. "fxspr/gui1-64.fxspr" */
    int          entry  = 0;                       /**< entry index within that file */
    std::string  provenance;                      /**< "bullfrog"|"keeperfx"|"mod" */
};

/** A fully resolved sprite identity + all of its variants. */
struct SpriteIdentity {
    std::string                id;                /**< stable "<base>/<entry>" */
    std::string                name;              /**< human-readable; may be empty */
    std::string                category;          /**< "gui","creature","texture",... */
    std::vector<std::string>   tags;
    std::vector<SpriteVariant> variants;          /**< sorted by scale then colour */
};

/** Loads and queries the sprite-pack manifests. Single shared instance. */
class VariantCatalogue {
public:
    static VariantCatalogue& instance();

    /** Load every FGrp_FxData/spritepacks/*.json (base pack + any mod packs).
     *  Clears any previously loaded data first. Returns true if at least one
     *  manifest loaded. Safe to call again to reload. */
    bool loadDefaultPacks();

    /** Merge one manifest by its FGrp_FxData-relative path
     *  (e.g. "spritepacks/base.json"). Returns true on success. */
    bool loadManifest(const char* fxdata_relpath);

    void clear();
    bool loaded() const { return m_loaded; }

    size_t collectionCount() const { return m_collections.size(); }

    /** Resolve base+entry into a full identity (variants filled with entry).
     *  Returns false if the base is unknown or entry is out of range. */
    bool resolve(const std::string& base, int entry, SpriteIdentity& out) const;

    /** Resolve a "<base>/<entry>" id string. */
    bool resolveId(const std::string& id, SpriteIdentity& out) const;

    /** Case-insensitive substring search over id + name. Returns matching ids
     *  (named overrides first, then id matches), capped at `limit`. */
    std::vector<std::string> search(const std::string& needle, size_t limit = 64) const;

    /** Bases of all known collections, for browsing / category filters. */
    std::vector<std::string> collectionBases() const;

    /** Category of a collection base ("" if unknown). */
    std::string categoryOf(const std::string& base) const;

private:
    struct Collection {
        std::string                base;
        std::string                category;
        std::string                provenance;
        int                        entries = 0;
        std::vector<SpriteVariant> variants;  /**< entry index filled per resolve */
    };
    struct Override {
        std::string                name;
        std::string                category;
        std::vector<std::string>   tags;
        std::vector<SpriteVariant> variants;  /**< extra variants layered on top */
    };

    bool parseManifest(const char* text, size_t len, const char* src);
    const Collection* findCollection(const std::string& base) const;

    std::vector<Collection>                    m_collections;
    std::unordered_map<std::string, Override>  m_overrides;   /**< id -> override */
    bool                                       m_loaded = false;
};

} // namespace kfx

#endif /* KEEPERFX_KFX_ASSETS_VARIANTCATALOGUE_H */
