/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SpriteSheetManager.cpp
 *     Centralised lifecycle manager for all atlas-registered TbSpriteSheet globals.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/assets/SpriteSheetManager.h"

#include "bflib_sprite.h"   /* load_spritesheet, free_spritesheet, num_sprites */
#include "bflib_basics.h"   /* ERRORLOG, SYNCLOG, ASSERT */
#include "bflib_fileio.h"   /* LbFileExists */
#include "config.h"         /* prepare_file_path, FGrp_StdData */
#include "kfx/profiling/KfxProfiling.h"

#include <stdio.h>          /* snprintf */

#include "post_inc.h"

/******************************************************************************/

SpriteSheetManager& SpriteSheetManager::Get()
{
    static SpriteSheetManager s_instance;
    return s_instance;
}

void SpriteSheetManager::Register(TbSpriteSheet** slot, const char* name)
{
    for (const auto& e : m_entries)
        if (e.slot == slot) return; // idempotent

    m_entries.push_back({slot, name});
    SYNCLOG("SpriteSheetManager: registered slot '%s' (%p)", name, (void*)slot);
}

void SpriteSheetManager::LoadCompanionFxSpr(Entry& e, const char* dat_path)
{
    e.fxspr = kfx::FxSprSheet();
    e.fxspr_loaded = false;
    if (!dat_path)
        return;

    // Derive the fxspr candidate from the .dat basename: the .fxspr file matches
    // the specific .dat currently loaded (e.g. gui1-32.dat -> fxspr/gui1-32.fxspr),
    // NOT the logical slot name (which can hold a 32- or 64-px .dat by resolution).
    const char* base = dat_path;
    for (const char* p = dat_path; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;

    char stem[64];
    size_t si = 0;
    for (const char* p = base; *p && *p != '.' && si + 1 < sizeof(stem); ++p)
        stem[si++] = *p;
    stem[si] = '\0';
    if (si == 0)
        return;

    char rel[96];
    snprintf(rel, sizeof(rel), "fxspr/%s.fxspr", stem);

    const char* resolved = prepare_file_path(FGrp_StdData, rel);
    if (!resolved)
        return;

    // Copy out of the shared prepare_file_path buffer before any further use.
    char full[DISKPATH_SIZE];
    snprintf(full, sizeof(full), "%s", resolved);

    // Most sheets have no .fxspr yet — probe first so absent files stay silent
    // (loadFromFile ERRORLOGs on a missing file, which would flood the log).
    if (!LbFileExists(full))
        return;

    if (e.fxspr.loadFromFile(full)) {
        e.fxspr_loaded = true;
        SYNCLOG("SpriteSheetManager: loaded truecolour companion '%s' (%d entries)",
                rel, e.fxspr.count());
    }
}

bool SpriteSheetManager::Load(TbSpriteSheet** slot,
                               const char* dat_path, const char* tab_path)
{
    KFX_ZONE_COLOR("SpriteSheetMgr::Load", KFX_COLOR_RENDER_CPU);

    Entry* entry = nullptr;
    for (auto& e : m_entries) if (e.slot == slot) { entry = &e; break; }
#ifdef BFDEBUG_LEVEL
    if (!entry) WARNLOG("SpriteSheetManager::Load: slot %p not registered — atlas rebuild will miss it", (void*)slot);
#endif

    if (*slot)
        free_spritesheet(slot);

    *slot = load_spritesheet(dat_path, tab_path);
    m_rebuild_pending = true;

    if (!*slot) {
        ERRORLOG("SpriteSheetManager: failed to load '%s'", dat_path);
        if (entry) { entry->fxspr = kfx::FxSprSheet(); entry->fxspr_loaded = false; }
        return false;
    }
    SYNCLOG("SpriteSheetManager: loaded '%s' into slot %p", dat_path, (void*)slot);
    if (entry)
        LoadCompanionFxSpr(*entry, dat_path);
    return true;
}

void SpriteSheetManager::Free(TbSpriteSheet** slot)
{
    KFX_ZONE_COLOR("SpriteSheetMgr::Free", KFX_COLOR_RENDER_CPU);

    Entry* entry = nullptr;
    for (auto& e : m_entries) if (e.slot == slot) { entry = &e; break; }
#ifdef BFDEBUG_LEVEL
    if (!entry) WARNLOG("SpriteSheetManager::Free: slot %p not registered", (void*)slot);
#endif

    if (entry) { entry->fxspr = kfx::FxSprSheet(); entry->fxspr_loaded = false; }

    if (!*slot) return;
    free_spritesheet(slot);
    m_rebuild_pending = true;
    SYNCLOG("SpriteSheetManager: freed slot %p", (void*)slot);
}

void SpriteSheetManager::FreeAll()
{
    KFX_ZONE_COLOR("SpriteSheetMgr::FreeAll", KFX_COLOR_RENDER_CPU);

    for (auto& e : m_entries) {
        e.fxspr = kfx::FxSprSheet();
        e.fxspr_loaded = false;
        if (*e.slot) {
            free_spritesheet(e.slot);
            SYNCLOG("SpriteSheetManager: freed '%s' in FreeAll", e.name);
        }
    }
    m_rebuild_pending = true;
}

void SpriteSheetManager::ScheduleRebuild()
{
    m_rebuild_pending = true;
}

int SpriteSheetManager::CollectActive(const TbSpriteSheet**   out_sheets,
                                       const char**            out_names,
                                       int                     capacity,
                                       const kfx::FxSprSheet** out_fxspr) const
{
    int n = 0;
    for (const auto& e : m_entries) {
        if (n >= capacity) break;
        const TbSpriteSheet* s = *e.slot;
        if (s && num_sprites(s) > 0) {
            out_sheets[n] = s;
            out_names[n]  = e.name;
            if (out_fxspr)
                out_fxspr[n] = e.fxspr_loaded ? &e.fxspr : nullptr;
            ++n;
        }
    }
    return n;
}

const kfx::FxSprSheet* SpriteSheetManager::FxSprForSlot(TbSpriteSheet** slot) const
{
    for (const auto& e : m_entries)
        if (e.slot == slot)
            return e.fxspr_loaded ? &e.fxspr : nullptr;
    return nullptr;
}

/******************************************************************************/
// C shims

extern "C" {

TbBool SpriteSheetMgr_Load(TbSpriteSheet** slot,
                             const char* dat_path, const char* tab_path)
{
    return SpriteSheetManager::Get().Load(slot, dat_path, tab_path) ? 1 : 0;
}

void SpriteSheetMgr_Free(TbSpriteSheet** slot)
{
    SpriteSheetManager::Get().Free(slot);
}

void SpriteSheetMgr_ScheduleRebuild(void)
{
    SpriteSheetManager::Get().ScheduleRebuild();
}

} // extern "C"
