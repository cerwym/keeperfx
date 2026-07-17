/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SpriteSheetManager.h
 *     Centralised lifecycle manager for all atlas-registered TbSpriteSheet globals.
 *
 *     Eliminates the stale-pointer aliasing bug that occurs when a sheet is freed
 *     and reallocated at the same address: the manager always drives a full
 *     Rebuild() through the registry of TbSpriteSheet** slots, so
 *     m_sprite_to_handle is always cleared before re-packing.
 *
 *     Owns two pieces of state that were previously raw statics in RendererManager:
 *       - m_rebuild_pending  (was s_rebuild_deferred)
 *       - m_gui_dirty        (was s_gui_sprites_dirty)
 *
 *     Contract:
 *       - Register() every managed slot once at renderer setup (idempotent).
 *       - Use Load() / Free() instead of load_spritesheet() / free_spritesheet()
 *         directly.  Both functions schedule a rebuild automatically.
 *       - All calls must be on the game thread.
 */
/******************************************************************************/
#pragma once

#include "bflib_sprite.h"   /* TbSpriteSheet */
#include "bflib_basics.h"   /* TbBool */

#ifdef __cplusplus
#include <vector>

#include "kfx/assets/FxSprSheet.h"   /* kfx::FxSprSheet — truecolour companion */

class SpriteSheetManager {
public:
    static SpriteSheetManager& Get();

    /** Register a C global slot as atlas-managed.  Idempotent — safe to call on
     *  renderer reinit; duplicate registrations (same ptr address) are ignored. */
    void Register(TbSpriteSheet** slot, const char* name);

    /** Free any existing content in slot, load a new sheet from disk, schedule
     *  an atlas rebuild.  Returns true on success; slot is NULL on failure. */
    bool Load(TbSpriteSheet** slot, const char* dat_path, const char* tab_path);

    /** Free slot content and schedule an atlas rebuild.  No-op when slot is NULL. */
    void Free(TbSpriteSheet** slot);

    /** Free ALL registered slots and schedule a single rebuild. */
    void FreeAll();

    /** Explicitly schedule an atlas rebuild without a load/free (e.g. after
     *  custom sheet construction that bypasses Load()). */
    void ScheduleRebuild();

    bool RebuildPending()      const { return m_rebuild_pending; }
    void ClearRebuildPending()       { m_rebuild_pending = false; }
    size_t RegisteredCount()   const { return m_entries.size(); }

    /** Collect currently-loaded (non-NULL, non-empty) sheets into out arrays.
     *  Returns the count written.
     *  @param out_fxspr  Optional (may be NULL). When provided, receives the
     *      companion truecolour `.fxspr` sheet for each collected slot, or NULL
     *      for slots with no loaded `.fxspr`. Parallel to out_sheets/out_names. */
    int CollectActive(const TbSpriteSheet**    out_sheets,
                      const char**             out_names,
                      int                      capacity,
                      const kfx::FxSprSheet**  out_fxspr = nullptr) const;

    /** Companion truecolour sheet loaded alongside the indexed sheet in `slot`,
     *  or NULL if none is loaded (no `.fxspr` on disk, or load failed). */
    const kfx::FxSprSheet* FxSprForSlot(TbSpriteSheet** slot) const;

    // ── GUI dirty flag ────────────────────────────────────────────────────────
    // Set by full GUI data loads (LoadVRes256Data / LoadVResMinimal / LoadMcgaData)
    // to signal that init_gui() must run after the next level load.

    /** Mark that a full GUI sprite reload occurred — init_gui() needed. */
    void MarkGUIDirty()  { m_gui_dirty = true; }

    /** Returns true if GUI reinit is needed; resets the flag.  Consumes once. */
    bool ConsumeGUIDirty() { bool v = m_gui_dirty; m_gui_dirty = false; return v; }

private:
    struct Entry {
        TbSpriteSheet** slot;
        const char*     name;
        kfx::FxSprSheet fxspr;              // companion truecolour sheet (move-only)
        bool            fxspr_loaded = false; // true once a valid .fxspr is adopted
    };

    /** Attempt to load the `.fxspr` companion for `dat_path` into `e.fxspr`.
     *  Derives `fxspr/<stem>.fxspr` from the dat basename and resolves it under
     *  the standard data group. Silent no-op (leaves fxspr invalid) when the
     *  file is absent — most sheets have no `.fxspr` yet. */
    void LoadCompanionFxSpr(Entry& e, const char* dat_path);

    std::vector<Entry> m_entries;
    bool  m_rebuild_pending = false;
    bool  m_gui_dirty       = true;  // true at startup — first load always reinits
};

#endif // __cplusplus

// ── C-callable shims for .c translation units ────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

TbBool SpriteSheetMgr_Load(struct TbSpriteSheet** slot,
                            const char* dat_path, const char* tab_path);
void   SpriteSheetMgr_Free(struct TbSpriteSheet** slot);
void   SpriteSheetMgr_ScheduleRebuild(void);

#ifdef __cplusplus
}
#endif
