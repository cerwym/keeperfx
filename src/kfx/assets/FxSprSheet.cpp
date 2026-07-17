/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprSheet.cpp
 *     `.fxspr` truecolour sprite container reader — implementation (v1 + v2).
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/assets/FxSprSheet.h"

#include <cstdio>
#include <cstring>

#include <zlib.h>

#include "bflib_basics.h"

#include "post_inc.h"
/******************************************************************************/

namespace kfx {

bool FxSprSheet::loadFromFile(const char* path)
{
    m_valid = false;
    m_blob.clear();

    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        LbErrorLog("FxSprSheet: cannot open '%s'\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        LbErrorLog("FxSprSheet: '%s' is empty\n", path);
        fclose(f);
        return false;
    }
    std::vector<uint8_t> bytes((size_t)len);
    const size_t got = fread(bytes.data(), 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
        LbErrorLog("FxSprSheet: short read on '%s' (%zu/%ld)\n", path, got, len);
        return false;
    }
    if (!loadFromMemory(std::move(bytes))) {
        LbErrorLog("FxSprSheet: '%s' is not a valid .fxspr\n", path);
        return false;
    }
    LbSyncLog("FxSprSheet: loaded '%s' (%d sprites, v%u)\n", path, count(), (unsigned)m_info.version);
    return true;
}

bool FxSprSheet::loadFromMemory(std::vector<uint8_t>&& bytes)
{
    m_valid = false;
    m_blob = std::move(bytes);
    m_inflated.clear();
    m_entries.clear();
    m_strings.assign(1, '\0'); // byte 0 is the empty string
    m_payload = nullptr;
    m_payload_size = 0;
    m_info = FxSprInfo{};
    m_valid = parse();
    if (!m_valid) {
        m_blob.clear();
        m_inflated.clear();
        m_entries.clear();
    }
    return m_valid;
}

const char* FxSprSheet::stringAt(uint32_t off) const
{
    if (off == 0 || off >= m_strings.size())
        return "";
    // Ensure the range is NUL-terminated somewhere within the table.
    return m_strings.data() + off;
}

bool FxSprSheet::parse()
{
    if (m_blob.size() < (size_t)FXSPR_HEADER_SIZE)
        return false;

    std::memcpy(&m_header, m_blob.data(), sizeof(FxSprHeader));

    if (m_header.magic[0] != FXSPR_MAGIC0 || m_header.magic[1] != FXSPR_MAGIC1 ||
        m_header.magic[2] != FXSPR_MAGIC2 || m_header.magic[3] != FXSPR_MAGIC3)
        return false;

    m_info.version = m_header.version;
    m_info.kind    = m_header.kind;

    if (m_header.version == FXSPR_VERSION_V1)
        return parseV1();
    if (m_header.version == FXSPR_VERSION)
        return parseV2();
    return false;
}

bool FxSprSheet::parseV1()
{
    const uint64_t blob_sz = (uint64_t)m_blob.size();
    const uint64_t dir_off = m_header.directory_off;
    const uint64_t pay_off = m_header.payload_off;
    const uint64_t count   = m_header.entry_count;

    if (dir_off + count * (uint64_t)FXSPR_ENTRY_SIZE > blob_sz)
        return false;
    if (pay_off > blob_sz)
        return false;

    m_payload      = m_blob.data() + (size_t)pay_off;
    m_payload_size = (size_t)(blob_sz - pay_off);

    m_entries.resize((size_t)count);
    for (uint64_t i = 0; i < count; ++i) {
        FxSprEntry e{};
        std::memcpy(&e, m_blob.data() + dir_off + i * (uint64_t)FXSPR_ENTRY_SIZE,
                    sizeof(FxSprEntry));
        const uint64_t span = (uint64_t)e.width * (uint64_t)e.height * (uint64_t)FXSPR_BYTES_PER_PIXEL;
        if ((uint64_t)e.data_off + span > m_payload_size)
            return false;
        FxSprEntryRich& r = m_entries[(size_t)i];
        r = FxSprEntryRich{};
        r.data_off = e.data_off;
        r.width    = e.width;
        r.height   = e.height;
        r.group_id = (uint32_t)i;
    }
    return true;
}

bool FxSprSheet::parseV2()
{
    const uint64_t blob_sz = (uint64_t)m_blob.size();
    if (blob_sz < (uint64_t)(FXSPR_HEADER_SIZE + FXSPR_HEADER_EXT2_SIZE))
        return false;

    FxSprHeaderExt2 ext{};
    std::memcpy(&ext, m_blob.data() + FXSPR_HEADER_SIZE, sizeof(FxSprHeaderExt2));

    const uint16_t stride = ext.entry_stride;
    if (stride != FXSPR_ENTRY_RICH_SIZE)
        return false;

    const uint64_t dir_off = m_header.directory_off;
    const uint64_t pay_off = m_header.payload_off;
    const uint64_t count   = m_header.entry_count;

    if (dir_off + count * (uint64_t)stride > blob_sz)
        return false;
    if ((uint64_t)pay_off + ext.payload_size > blob_sz)
        return false;

    // ── String table ─────────────────────────────────────────────────────────
    if (ext.stringtable_off != 0 && ext.stringtable_size != 0) {
        if ((uint64_t)ext.stringtable_off + ext.stringtable_size > blob_sz)
            return false;
        m_strings.assign((size_t)ext.stringtable_size, '\0');
        std::memcpy(m_strings.data(), m_blob.data() + ext.stringtable_off,
                    (size_t)ext.stringtable_size);
        m_strings.back() = '\0'; // guarantee termination of the last string
    } else {
        m_strings.assign(1, '\0');
    }

    // ── Asset-info block ──────────────────────────────────────────────────────
    if (ext.assetinfo_off != 0 && ext.assetinfo_size >= (uint16_t)FXSPR_ASSETINFO_SIZE) {
        if ((uint64_t)ext.assetinfo_off + FXSPR_ASSETINFO_SIZE > blob_sz)
            return false;
        FxSprAssetInfo ai{};
        std::memcpy(&ai, m_blob.data() + ext.assetinfo_off, sizeof(FxSprAssetInfo));
        m_info.scale       = ai.scale;
        m_info.colour_mode = ai.colour_mode;
        m_info.provenance  = ai.provenance;
        m_info.name        = stringAt(ai.name_off);
    }

    // ── Payload (optionally zlib-compressed) ──────────────────────────────────
    const uint8_t* on_disk = m_blob.data() + (size_t)pay_off;
    if ((m_header.flags & FxSprFlag_PayloadCompressed) != 0) {
        m_inflated.resize((size_t)ext.payload_raw_size);
        uLongf dest_len = (uLongf)ext.payload_raw_size;
        const int zr = uncompress(m_inflated.data(), &dest_len,
                                  on_disk, (uLong)ext.payload_size);
        if (zr != Z_OK || dest_len != (uLongf)ext.payload_raw_size) {
            LbErrorLog("FxSprSheet: zlib inflate failed (%d, %lu/%u)\n",
                       zr, (unsigned long)dest_len, (unsigned)ext.payload_raw_size);
            return false;
        }
        m_payload      = m_inflated.data();
        m_payload_size = m_inflated.size();
    } else {
        if ((uint64_t)pay_off + ext.payload_raw_size > blob_sz)
            return false;
        m_payload      = on_disk;
        m_payload_size = (size_t)ext.payload_raw_size;
    }

    // ── Directory (rich entries, validated against the inflated payload) ──────
    m_entries.resize((size_t)count);
    for (uint64_t i = 0; i < count; ++i) {
        FxSprEntryRich r{};
        std::memcpy(&r, m_blob.data() + dir_off + i * (uint64_t)stride,
                    sizeof(FxSprEntryRich));
        const uint64_t span = (uint64_t)r.width * (uint64_t)r.height * (uint64_t)FXSPR_BYTES_PER_PIXEL;
        if ((uint64_t)r.data_off + span > (uint64_t)m_payload_size)
            return false;
        m_entries[(size_t)i] = r;
    }
    return true;
}

bool FxSprSheet::sprite(int index, FxSprSprite& out) const
{
    out = FxSprSprite{};
    if (!m_valid || index < 0 || index >= (int)m_entries.size())
        return false;

    const FxSprEntryRich& e = m_entries[(size_t)index];
    out.width  = e.width;
    out.height = e.height;
    if (e.width == 0 || e.height == 0) {
        out.rgba = nullptr; // sentinel / empty entry
        return true;
    }
    out.rgba = m_payload + (size_t)e.data_off;
    return true;
}

bool FxSprSheet::meta(int index, FxSprMeta& out) const
{
    out = FxSprMeta{};
    if (!m_valid || index < 0 || index >= (int)m_entries.size())
        return false;

    const FxSprEntryRich& e = m_entries[(size_t)index];
    out.width         = e.width;
    out.height        = e.height;
    out.offset_x      = e.offset_x;
    out.offset_y      = e.offset_y;
    out.shadow_offset = e.shadow_offset;
    out.frame_flags   = e.frame_flags;
    out.group_id      = e.group_id;
    out.frame_index   = e.frame_index;
    out.rotation      = e.rotation;
    out.view          = e.view;
    out.category      = e.category;
    out.name          = stringAt(e.name_off);
    return true;
}

const char* FxSprSheet::name(int index) const
{
    if (!m_valid || index < 0 || index >= (int)m_entries.size())
        return "";
    return stringAt(m_entries[(size_t)index].name_off);
}

} // namespace kfx
