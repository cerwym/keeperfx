/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SpriteSheetManager.cpp
 *     Centralised lifecycle manager for all atlas-registered TbSpriteSheet globals.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/SpriteSheetManager.h"

#include "bflib_sprite.h"   /* load_spritesheet, free_spritesheet, num_sprites */
#include "bflib_basics.h"   /* ERRORLOG, SYNCLOG, ASSERT */
#include "kfx/profiling/KfxProfiling.h"

#include "post_inc.h"

/******************************************************************************/

SpriteSheetManager& SpriteSheetManager::Get()
{
    static SpriteSheetManager s_instance;
    return s_instance;
}

void SpriteSheetManager::Register(TbSpriteSheet** slot, const char* name)
{
    for (int i = 0; i < m_count; ++i)
        if (m_entries[i].slot == slot) return; // idempotent

    if (m_count >= kMaxSheets) {
        ERRORLOG("SpriteSheetManager::Register: capacity exceeded (max %d)", kMaxSheets);
        return;
    }
    m_entries[m_count++] = {slot, name};
    SYNCLOG("SpriteSheetManager: registered slot '%s' (%p)", name, (void*)slot);
}

bool SpriteSheetManager::Load(TbSpriteSheet** slot,
                               const char* dat_path, const char* tab_path)
{
    KFX_ZONE_COLOR("SpriteSheetMgr::Load", KFX_COLOR_RENDER_CPU);

#ifdef BFDEBUG_LEVEL
    // Guard against callers that bypass Register() — would cause silent atlas misses.
    bool found = false;
    for (int i = 0; i < m_count; ++i) if (m_entries[i].slot == slot) { found = true; break; }
    if (!found) WARNLOG("SpriteSheetManager::Load: slot %p not registered — atlas rebuild will miss it", (void*)slot);
#endif

    if (*slot)
        free_spritesheet(slot);

    *slot = load_spritesheet(dat_path, tab_path);
    m_rebuild_pending = true;

    if (!*slot) {
        ERRORLOG("SpriteSheetManager: failed to load '%s'", dat_path);
        return false;
    }
    SYNCLOG("SpriteSheetManager: loaded '%s' into slot %p", dat_path, (void*)slot);
    return true;
}

void SpriteSheetManager::Free(TbSpriteSheet** slot)
{
    KFX_ZONE_COLOR("SpriteSheetMgr::Free", KFX_COLOR_RENDER_CPU);

#ifdef BFDEBUG_LEVEL
    bool found = false;
    for (int i = 0; i < m_count; ++i) if (m_entries[i].slot == slot) { found = true; break; }
    if (!found) WARNLOG("SpriteSheetManager::Free: slot %p not registered", (void*)slot);
#endif

    if (!*slot) return;
    free_spritesheet(slot);
    m_rebuild_pending = true;
    SYNCLOG("SpriteSheetManager: freed slot %p", (void*)slot);
}

void SpriteSheetManager::FreeAll()
{
    KFX_ZONE_COLOR("SpriteSheetMgr::FreeAll", KFX_COLOR_RENDER_CPU);

    for (int i = 0; i < m_count; ++i) {
        if (*m_entries[i].slot) {
            free_spritesheet(m_entries[i].slot);
            SYNCLOG("SpriteSheetManager: freed '%s' in FreeAll", m_entries[i].name);
        }
    }
    m_rebuild_pending = true; // one rebuild covers all
}

void SpriteSheetManager::ScheduleRebuild()
{
    m_rebuild_pending = true;
}

int SpriteSheetManager::CollectActive(const TbSpriteSheet** out_sheets,
                                       const char**          out_names,
                                       int                   capacity) const
{
    int n = 0;
    for (int i = 0; i < m_count && n < capacity; ++i) {
        const TbSpriteSheet* s = *m_entries[i].slot;
        if (s && num_sprites(s) > 0)
            { out_sheets[n] = s; out_names[n++] = m_entries[i].name; }
    }
    return n;
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
