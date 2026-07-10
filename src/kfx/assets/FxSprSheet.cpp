/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprSheet.cpp
 *     `.fxspr` truecolour sprite container reader — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/assets/FxSprSheet.h"

#include <cstdio>
#include <cstring>

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
    LbSyncLog("FxSprSheet: loaded '%s' (%d sprites)\n", path, count());
    return true;
}

bool FxSprSheet::loadFromMemory(std::vector<uint8_t>&& bytes)
{
    m_valid = false;
    m_blob = std::move(bytes);
    m_valid = parse();
    if (!m_valid)
        m_blob.clear();
    return m_valid;
}

bool FxSprSheet::parse()
{
    if (m_blob.size() < (size_t)FXSPR_HEADER_SIZE)
        return false;

    std::memcpy(&m_header, m_blob.data(), sizeof(FxSprHeader));

    if (m_header.magic[0] != FXSPR_MAGIC0 || m_header.magic[1] != FXSPR_MAGIC1 ||
        m_header.magic[2] != FXSPR_MAGIC2 || m_header.magic[3] != FXSPR_MAGIC3)
        return false;
    if (m_header.version != FXSPR_VERSION)
        return false;

    const uint64_t blob_sz = (uint64_t)m_blob.size();
    const uint64_t dir_off = m_header.directory_off;
    const uint64_t pay_off = m_header.payload_off;
    const uint64_t count   = m_header.entry_count;

    // Directory must fit.
    if (dir_off + count * (uint64_t)FXSPR_ENTRY_SIZE > blob_sz)
        return false;
    if (pay_off > blob_sz)
        return false;

    // Validate every entry's payload span up front so accessors can trust it.
    const uint64_t payload_bytes = blob_sz - pay_off;
    for (uint64_t i = 0; i < count; ++i) {
        FxSprEntry e{};
        std::memcpy(&e, m_blob.data() + dir_off + i * (uint64_t)FXSPR_ENTRY_SIZE,
                    sizeof(FxSprEntry));
        const uint64_t span = (uint64_t)e.width * (uint64_t)e.height * (uint64_t)FXSPR_BYTES_PER_PIXEL;
        if ((uint64_t)e.data_off + span > payload_bytes)
            return false;
    }
    return true;
}

bool FxSprSheet::sprite(int index, FxSprSprite& out) const
{
    out = FxSprSprite{};
    if (!m_valid || index < 0 || index >= (int)m_header.entry_count)
        return false;

    FxSprEntry e{};
    std::memcpy(&e,
                m_blob.data() + (size_t)m_header.directory_off + (size_t)index * FXSPR_ENTRY_SIZE,
                sizeof(FxSprEntry));

    out.width  = e.width;
    out.height = e.height;
    if (e.width == 0 || e.height == 0) {
        out.rgba = nullptr; // sentinel / empty entry
        return true;
    }
    out.rgba = m_blob.data() + (size_t)m_header.payload_off + (size_t)e.data_off;
    return true;
}

} // namespace kfx
