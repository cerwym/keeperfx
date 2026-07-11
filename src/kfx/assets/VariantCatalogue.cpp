/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file VariantCatalogue.cpp
 *     Sprite identity + quality-variant catalogue — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/assets/VariantCatalogue.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"

#include <value.h>
#include <json-dom.h>

#include "post_inc.h"
/******************************************************************************/

namespace kfx {

namespace {

std::string to_lower(const std::string& s)
{
    std::string out(s);
    for (char& c : out)
        c = (char)std::tolower((unsigned char)c);
    return out;
}

const char* dict_str(const VALUE* dict, const char* key)
{
    const char* s = value_string(value_dict_get((VALUE*)dict, key));
    return s ? s : "";
}

int dict_int(const VALUE* dict, const char* key)
{
    VALUE* v = value_dict_get((VALUE*)dict, key);
    if (v == nullptr || value_type(v) == VALUE_NULL)
        return 0;
    return value_int32(v);
}

FxColourKind colour_from(const char* s)
{
    return (s != nullptr && std::strcmp(s, "truecolour") == 0)
               ? FxColourKind::Truecolour
               : FxColourKind::Indexed;
}

bool read_whole_file(const char* path, std::string& out)
{
    FILE* f = fopen(path, "rb");
    if (f == nullptr)
        return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)len);
    const size_t got = fread(&out[0], 1, (size_t)len, f);
    fclose(f);
    return got == (size_t)len;
}

void parse_variants(const VALUE* arr, const std::string& provenance,
                    std::vector<SpriteVariant>& out)
{
    if (arr == nullptr || value_type((VALUE*)arr) != VALUE_ARRAY)
        return;
    const size_t n = value_array_size((VALUE*)arr);
    for (size_t j = 0; j < n; ++j) {
        VALUE* v = value_array_get((VALUE*)arr, j);
        if (v == nullptr || value_type(v) != VALUE_DICT)
            continue;
        SpriteVariant sv;
        sv.scale      = dict_int(v, "scale");
        sv.colour     = colour_from(value_string(value_dict_get(v, "colour")));
        sv.file       = dict_str(v, "file");
        sv.entry      = dict_int(v, "entry");
        sv.provenance = provenance;
        out.push_back(std::move(sv));
    }
}

} // namespace

VariantCatalogue& VariantCatalogue::instance()
{
    static VariantCatalogue s_instance;
    return s_instance;
}

void VariantCatalogue::clear()
{
    m_collections.clear();
    m_overrides.clear();
    m_loaded = false;
}

const VariantCatalogue::Collection*
VariantCatalogue::findCollection(const std::string& base) const
{
    for (const Collection& c : m_collections)
        if (c.base == base)
            return &c;
    return nullptr;
}

bool VariantCatalogue::loadDefaultPacks()
{
    clear();

    // Enumerate FGrp_FxData/spritepacks/*.json. prepare_file_path returns a
    // shared static buffer, so copy each resolved name before re-calling it.
    char* spec = prepare_file_path(FGrp_FxData, "spritepacks/*.json");
    if (spec == nullptr)
        return false;

    std::vector<std::string> names;
    struct TbFileEntry fe = {nullptr};
    struct TbFileFind* ff = LbFileFindFirst(spec, &fe);
    if (ff != nullptr) {
        do {
            if (fe.Filename != nullptr)
                names.emplace_back(fe.Filename);
        } while (LbFileFindNext(ff, &fe) >= 0);
        LbFileFindEnd(ff);
    }

    // Deterministic order: base.json first, then the rest alphabetically, so
    // mod packs layer over the base predictably.
    std::sort(names.begin(), names.end());
    std::stable_partition(names.begin(), names.end(),
                          [](const std::string& n) { return n == "base.json"; });

    int ok = 0;
    for (const std::string& n : names) {
        std::string rel = "spritepacks/" + n;
        if (loadManifest(rel.c_str()))
            ++ok;
    }
    m_loaded = ok > 0;
    LbSyncLog("VariantCatalogue: loaded %d manifest(s), %zu collection(s)\n",
              ok, m_collections.size());
    return m_loaded;
}

bool VariantCatalogue::loadManifest(const char* fxdata_relpath)
{
    char* path = prepare_file_path(FGrp_FxData, fxdata_relpath);
    if (path == nullptr)
        return false;

    std::string text;
    if (!read_whole_file(path, text)) {
        LbWarnLog("VariantCatalogue: cannot read manifest '%s'\n", fxdata_relpath);
        return false;
    }
    return parseManifest(text.c_str(), text.size(), fxdata_relpath);
}

bool VariantCatalogue::parseManifest(const char* text, size_t len, const char* src)
{
    VALUE root;
    const int ret = json_dom_parse(text, len, nullptr, 0, &root, nullptr);
    if (ret != 0) {
        LbErrorLog("VariantCatalogue: JSON parse error in '%s' (%d)\n", src, ret);
        return false;
    }

    std::string provenance = "unknown";
    VALUE* pack = value_dict_get(&root, "pack");
    if (pack != nullptr && value_type(pack) == VALUE_DICT) {
        const char* p = value_string(value_dict_get(pack, "provenance"));
        if (p != nullptr && p[0] != '\0')
            provenance = p;
    }

    // ── Collections ───────────────────────────────────────────────────────────
    VALUE* collections = value_dict_get(&root, "collections");
    if (collections != nullptr && value_type(collections) == VALUE_ARRAY) {
        const size_t n = value_array_size(collections);
        for (size_t i = 0; i < n; ++i) {
            VALUE* c = value_array_get(collections, i);
            if (c == nullptr || value_type(c) != VALUE_DICT)
                continue;
            const std::string base = dict_str(c, "base");
            if (base.empty())
                continue;

            std::vector<SpriteVariant> variants;
            parse_variants(value_dict_get(c, "variants"), provenance, variants);
            const int entries = dict_int(c, "entries");
            const std::string category = dict_str(c, "category");

            // Merge into an existing collection (mod pack adds variants) or add.
            auto it = std::find_if(m_collections.begin(), m_collections.end(),
                                   [&](const Collection& x) { return x.base == base; });
            if (it == m_collections.end()) {
                Collection col;
                col.base       = base;
                col.category   = category;
                col.provenance = provenance;
                col.entries    = entries;
                col.variants   = std::move(variants);
                m_collections.push_back(std::move(col));
            } else {
                it->entries = std::max(it->entries, entries);
                for (SpriteVariant& v : variants)
                    it->variants.push_back(std::move(v));
                if (it->category.empty())
                    it->category = category;
            }
        }
    }

    // ── Per-entry overrides (names, tags, extra variants) ─────────────────────
    VALUE* sprites = value_dict_get(&root, "sprites");
    if (sprites != nullptr && value_type(sprites) == VALUE_ARRAY) {
        const size_t n = value_array_size(sprites);
        for (size_t i = 0; i < n; ++i) {
            VALUE* s = value_array_get(sprites, i);
            if (s == nullptr || value_type(s) != VALUE_DICT)
                continue;
            const std::string id = dict_str(s, "id");
            if (id.empty())
                continue;
            Override& ov = m_overrides[id];
            const char* nm = value_string(value_dict_get(s, "name"));
            if (nm != nullptr && nm[0] != '\0')
                ov.name = nm;
            const char* cat = value_string(value_dict_get(s, "category"));
            if (cat != nullptr && cat[0] != '\0')
                ov.category = cat;
            VALUE* tags = value_dict_get(s, "tags");
            if (tags != nullptr && value_type(tags) == VALUE_ARRAY) {
                const size_t tn = value_array_size(tags);
                for (size_t t = 0; t < tn; ++t) {
                    const char* ts = value_string(value_array_get(tags, t));
                    if (ts != nullptr && ts[0] != '\0')
                        ov.tags.emplace_back(ts);
                }
            }
            parse_variants(value_dict_get(s, "variants"), provenance, ov.variants);
        }
    }

    value_fini(&root);
    return true;
}

bool VariantCatalogue::resolve(const std::string& base, int entry, SpriteIdentity& out) const
{
    const Collection* col = findCollection(base);
    if (col == nullptr)
        return false;
    if (entry < 0 || (col->entries > 0 && entry >= col->entries))
        return false;

    out = SpriteIdentity{};
    out.id       = base + "/" + std::to_string(entry);
    out.category = col->category;

    for (const SpriteVariant& v : col->variants) {
        SpriteVariant sv = v;
        sv.entry = entry;   // synthesised: this entry index in each variant file
        out.variants.push_back(std::move(sv));
    }

    // Layer any per-entry override.
    auto it = m_overrides.find(out.id);
    if (it != m_overrides.end()) {
        const Override& ov = it->second;
        if (!ov.name.empty())
            out.name = ov.name;
        if (!ov.category.empty())
            out.category = ov.category;
        out.tags = ov.tags;
        for (const SpriteVariant& v : ov.variants)
            out.variants.push_back(v);
    }

    std::sort(out.variants.begin(), out.variants.end(),
              [](const SpriteVariant& a, const SpriteVariant& b) {
                  if (a.scale != b.scale)
                      return a.scale < b.scale;
                  return (int)a.colour < (int)b.colour;
              });
    return true;
}

bool VariantCatalogue::resolveId(const std::string& id, SpriteIdentity& out) const
{
    const size_t slash = id.rfind('/');
    if (slash == std::string::npos || slash + 1 >= id.size())
        return false;
    const std::string base = id.substr(0, slash);
    const std::string idx  = id.substr(slash + 1);
    for (char c : idx)
        if (!std::isdigit((unsigned char)c))
            return false;
    return resolve(base, std::atoi(idx.c_str()), out);
}

std::vector<std::string> VariantCatalogue::search(const std::string& needle, size_t limit) const
{
    std::vector<std::string> hits;
    if (needle.empty())
        return hits;
    const std::string q = to_lower(needle);

    // Named overrides first (search hits on the human-readable name / id).
    for (const auto& kv : m_overrides) {
        if (hits.size() >= limit)
            break;
        const std::string& id = kv.first;
        const std::string name_l = to_lower(kv.second.name);
        if (to_lower(id).find(q) != std::string::npos ||
            (!name_l.empty() && name_l.find(q) != std::string::npos))
            hits.push_back(id);
    }

    // Then collection bases, so an unnamed sheet is still discoverable by base.
    for (const Collection& c : m_collections) {
        if (hits.size() >= limit)
            break;
        if (to_lower(c.base).find(q) != std::string::npos)
            hits.push_back(c.base + "/0");
    }
    return hits;
}

std::vector<std::string> VariantCatalogue::collectionBases() const
{
    std::vector<std::string> out;
    out.reserve(m_collections.size());
    for (const Collection& c : m_collections)
        out.push_back(c.base);
    return out;
}

std::string VariantCatalogue::categoryOf(const std::string& base) const
{
    const Collection* c = findCollection(base);
    return c ? c->category : std::string();
}

} // namespace kfx
