/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprAnimTable.h
 *     Engine-side parser for the optional .fxspr animation descriptor block
 *     (rotational-fidelity amendment) plus a generalized direction selector.
 *
 * @par Status
 *     COMPILED BUT NOT USED BY THE RENDERER. This carries the richer per-anim
 *     directionality (arbitrary direction counts / angles / mirroring) so the
 *     engine can parse and reason about it, while the three legacy draw sites
 *     keep using the hardcoded 5-group / abs(4-sector) math. A later change will
 *     switch the draw path to selectDir() behind a config flag. See
 *     docs/sprite-rotational-fidelity.md.
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_ASSETS_FXSPRANIMTABLE_H
#define KEEPERFX_KFX_ASSETS_FXSPRANIMTABLE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kfx {

/** One facing of one animation (decoded from FxSprAnimDir). */
struct KfxAnimDir {
    uint16_t angle       = 0;  /**< 0..2047 (== 0..360deg) this bitmap represents */
    uint8_t  mirror      = 0;  /**< 1 = horizontal mirror of base_dir */
    uint8_t  base_dir    = 0;  /**< local dir index to mirror when mirror==1 */
    uint32_t entry_first = 0;  /**< directory index of this direction's frame 0 */
};

/** One logical animation (decoded from FxSprAnim). Its directions are the
 *  dir_count entries at [dir_first, dir_first+dir_count) in the flat dir array. */
struct KfxAnim {
    uint32_t    group_id       = 0;
    uint16_t    frames         = 0;
    uint16_t    dir_first      = 0;
    uint16_t    dir_count      = 0;
    uint8_t     legacy_rotable = 0;
    uint8_t     view           = 0;
    uint16_t    fps            = 0;
    const char* name           = ""; /**< borrowed into the table's string copy */
};

/** Legacy 5-group selection, kept as the fallback and for parity tests.
 *  group = abs(4 - (((angle + DEGREES_22_5) & ANGLE_MASK) >> 8)), 0..4.
 *  Sets *out_xflip (non-null) for the mirrored left hemisphere. */
int fxspr_legacy_rot_group(int angle, int* out_xflip);

/** Parsed animation descriptor block for one .fxspr. Empty (valid()==false) for
 *  legacy files with no block, which is not an error. */
class FxSprAnimTable {
public:
    FxSprAnimTable() = default;

    /** Parse the anim block out of a whole .fxspr blob. Returns true only when a
     *  well-formed block was present and adopted. Malformed / absent -> false and
     *  the table stays empty. */
    bool parse(const uint8_t* blob, size_t size);

    void clear();

    bool valid()     const { return m_valid; }
    int  animCount() const { return (int)m_anims.size(); }
    int  dirCount()  const { return (int)m_dirs.size(); }

    /** Animation by index, or nullptr. */
    const KfxAnim* anim(int i) const;
    /** Find an animation by its group_id, or nullptr. */
    const KfxAnim* animByGroup(uint32_t group_id) const;
    /** Direction `local` (0..a.dir_count-1) of animation `a`, or nullptr. */
    const KfxAnimDir* animDir(const KfxAnim& a, int local) const;

    /** Generalized nearest-direction selector (superset of the legacy mapping).
     *  Returns the LOCAL direction index within `a` whose stored bitmap should be
     *  drawn for `angle` (0..2047), resolving mirror/base_dir so the returned dir
     *  always has real pixels; sets *out_mirror (non-null) to whether it must be
     *  drawn horizontally flipped. When the table is empty or `a` is null it
     *  falls back to the legacy 5-group result (group in *return*, xflip in
     *  *out_mirror). */
    int selectDir(const KfxAnim* a, int angle, int* out_mirror) const;

private:
    const char* stringAt(uint32_t off) const;

    std::vector<KfxAnim>    m_anims;
    std::vector<KfxAnimDir> m_dirs;
    std::vector<char>       m_strings;
    bool                    m_valid = false;
};

} // namespace kfx

#endif // KEEPERFX_KFX_ASSETS_FXSPRANIMTABLE_H
