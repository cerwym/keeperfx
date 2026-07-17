/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprAnimTable.cpp
 *     Engine-side parser for the .fxspr animation descriptor block — impl.
 *     COMPILED BUT NOT USED BY THE RENDERER (see header + design doc).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/assets/FxSprAnimTable.h"
#include "kfx/assets/FxSprAnimSelect.h"

#include "kfx/assets/FxSprFormat.h"
#include "config_keeperfx.h"

#include <cstring>

#include "post_inc.h"

namespace kfx {

// Lock the on-disk layout this parser assumes to the format header's declared
// sizes. If FxSprFormat.h ever changes a struct, this fails to compile loudly.
static_assert(sizeof(FxSprHeader)      == FXSPR_HEADER_SIZE,      "FxSprHeader size");
static_assert(sizeof(FxSprHeaderExt2)  == FXSPR_HEADER_EXT2_SIZE, "FxSprHeaderExt2 size");
static_assert(sizeof(FxSprHeaderExt3)  == FXSPR_HEADER_EXT3_SIZE, "FxSprHeaderExt3 size");
static_assert(sizeof(FxSprAnimBlock)   == FXSPR_ANIMBLOCK_SIZE,   "FxSprAnimBlock size");
static_assert(sizeof(FxSprAnim)        == FXSPR_ANIM_SIZE,        "FxSprAnim size");
static_assert(sizeof(FxSprAnimDir)     == FXSPR_ANIMDIR_SIZE,     "FxSprAnimDir size");

namespace {

constexpr int kAngleMask   = 2047; // ANGLE_MASK
constexpr int kDegrees22_5 = 128;  // DEGREES_22_5 == 2048/16

// Smallest wrapped distance between two 0..2047 angles (0..1024).
int angle_delta(int a, int b)
{
    int d = (a - b) & kAngleMask;
    if (d > (kAngleMask + 1) / 2)
        d = (kAngleMask + 1) - d;
    return d;
}

} // namespace

int fxspr_legacy_rot_group(int angle, int* out_xflip)
{
    const int a = (angle + kDegrees22_5) & kAngleMask;
    int g = 4 - (a >> 8);
    if (g < 0) g = -g;
    if (out_xflip != nullptr) {
        const int m = angle & kAngleMask;
        *out_xflip = (m > 1151 && m < 1919) ? 1 : 0; // DEGREES_202_5 .. DEGREES_337_5
    }
    return g;
}

void FxSprAnimTable::clear()
{
    m_anims.clear();
    m_dirs.clear();
    m_strings.assign(1, '\0');
    m_valid = false;
}

const char* FxSprAnimTable::stringAt(uint32_t off) const
{
    if (off == 0 || off >= m_strings.size())
        return "";
    return m_strings.data() + off;
}

const KfxAnim* FxSprAnimTable::anim(int i) const
{
    if (i < 0 || i >= (int)m_anims.size())
        return nullptr;
    return &m_anims[(size_t)i];
}

const KfxAnim* FxSprAnimTable::animByGroup(uint32_t group_id) const
{
    for (const KfxAnim& a : m_anims)
        if (a.group_id == group_id)
            return &a;
    return nullptr;
}

const KfxAnimDir* FxSprAnimTable::animDir(const KfxAnim& a, int local) const
{
    if (local < 0 || local >= (int)a.dir_count)
        return nullptr;
    const size_t idx = (size_t)a.dir_first + (size_t)local;
    if (idx >= m_dirs.size())
        return nullptr;
    return &m_dirs[idx];
}

int FxSprAnimTable::selectDir(const KfxAnim* a, int angle, int* out_mirror) const
{
    // Fallback: no extended table -> legacy 5-group mapping.
    if (!m_valid || a == nullptr || a->dir_count == 0)
        return fxspr_legacy_rot_group(angle, out_mirror);

    const int ang = angle & kAngleMask;
    int best = 0;
    int best_delta = kAngleMask + 1;
    for (int l = 0; l < (int)a->dir_count; ++l) {
        const KfxAnimDir* d = animDir(*a, l);
        if (d == nullptr)
            continue;
        const int delta = angle_delta(ang, (int)d->angle);
        if (delta < best_delta) {
            best_delta = delta;
            best = l;
        }
    }

    // Resolve a mirror direction to the real bitmap it flips.
    const KfxAnimDir* chosen = animDir(*a, best);
    if (chosen != nullptr && chosen->mirror) {
        if (out_mirror != nullptr) *out_mirror = 1;
        return (int)chosen->base_dir;
    }
    if (out_mirror != nullptr) *out_mirror = 0;
    return best;
}

bool FxSprAnimTable::parse(const uint8_t* blob, size_t size)
{
    clear();
    if (blob == nullptr)
        return false;
    if (size < (size_t)(FXSPR_HEADER_SIZE + FXSPR_HEADER_EXT2_SIZE + FXSPR_HEADER_EXT3_SIZE))
        return false;

    FxSprHeader hdr{};
    std::memcpy(&hdr, blob, sizeof(hdr));
    if (hdr.magic[0] != FXSPR_MAGIC0 || hdr.magic[1] != FXSPR_MAGIC1 ||
        hdr.magic[2] != FXSPR_MAGIC2 || hdr.magic[3] != FXSPR_MAGIC3)
        return false;
    if (hdr.version != FXSPR_VERSION)
        return false;
    if ((hdr.flags & FxSprFlag_AnimBlock) == 0)
        return false; // legacy file: no anim block (not an error)

    FxSprHeaderExt2 ext2{};
    std::memcpy(&ext2, blob + FXSPR_HEADER_SIZE, sizeof(ext2));

    FxSprHeaderExt3 ext3{};
    std::memcpy(&ext3, blob + FXSPR_HEADER_EXT3_OFF, sizeof(ext3));
    if (ext3.animblock_off == 0 || ext3.animblock_size == 0)
        return false;
    if ((uint64_t)ext3.animblock_off + ext3.animblock_size > size)
        return false;

    // Copy the string table for name resolution (optional).
    if (ext2.stringtable_off != 0 && ext2.stringtable_size != 0 &&
        (uint64_t)ext2.stringtable_off + ext2.stringtable_size <= size) {
        m_strings.assign((size_t)ext2.stringtable_size, '\0');
        std::memcpy(m_strings.data(), blob + ext2.stringtable_off,
                    (size_t)ext2.stringtable_size);
        m_strings.back() = '\0';
    }

    const uint8_t* base = blob + ext3.animblock_off;
    if (ext3.animblock_size < (uint32_t)FXSPR_ANIMBLOCK_SIZE)
        return false;

    FxSprAnimBlock ab{};
    std::memcpy(&ab, base, sizeof(ab));
    if (ab.version != FXSPR_ANIMBLOCK_VERSION)
        return false;
    if (ab.anim_stride < (uint16_t)FXSPR_ANIM_SIZE ||
        ab.dir_stride  < (uint16_t)FXSPR_ANIMDIR_SIZE)
        return false;

    // Bounds: [header][anims][dirs] must all fit within the declared block size.
    const uint64_t anims_off = (uint64_t)FXSPR_ANIMBLOCK_SIZE;
    const uint64_t anims_end = anims_off + (uint64_t)ab.anim_count * ab.anim_stride;
    const uint64_t dirs_off  = anims_end;
    const uint64_t dirs_end  = dirs_off + (uint64_t)ab.dir_count * ab.dir_stride;
    if (dirs_end > ext3.animblock_size)
        return false;

    m_dirs.resize((size_t)ab.dir_count);
    for (uint32_t i = 0; i < ab.dir_count; ++i) {
        FxSprAnimDir d{};
        std::memcpy(&d, base + dirs_off + (uint64_t)i * ab.dir_stride, sizeof(d));
        KfxAnimDir& out = m_dirs[(size_t)i];
        out.angle       = d.angle;
        out.mirror      = d.mirror;
        out.base_dir    = d.base_dir;
        out.entry_first = d.entry_first;
    }

    m_anims.resize((size_t)ab.anim_count);
    for (uint32_t i = 0; i < ab.anim_count; ++i) {
        FxSprAnim a{};
        std::memcpy(&a, base + anims_off + (uint64_t)i * ab.anim_stride, sizeof(a));
        // Validate the direction slice references the flat dir array.
        if ((uint64_t)a.dir_first + a.dir_count > ab.dir_count) {
            clear();
            return false;
        }
        KfxAnim& out = m_anims[(size_t)i];
        out.group_id       = a.group_id;
        out.frames         = a.frames;
        out.dir_first      = a.dir_first;
        out.dir_count      = a.dir_count;
        out.legacy_rotable = a.legacy_rotable;
        out.view           = a.view;
        out.fps            = a.fps;
        out.name           = stringAt(a.name_off);
    }

    m_valid = true;
    return true;
}

} // namespace kfx

extern "C" int kfx_anim_select_dir_group(int angle, int* out_mirror)
{
    if (cfg_fxspr_anim_select_dir != 0) {
        // Hook point for future runtime .fxspr anim-table consumption. Until
        // the runtime path supplies per-animation tables, this intentionally
        // resolves through selectDir's legacy fallback.
        static kfx::FxSprAnimTable s_anim_table;
        return s_anim_table.selectDir(nullptr, angle, out_mirror);
    }
    return kfx::fxspr_legacy_rot_group(angle, out_mirror);
}
