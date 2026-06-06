/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file FontManager.cpp
 *     Lifecycle manager for all text-font TbSpriteSheet globals.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/FontManager.h"

#include "bflib_sprite.h"   /* load_spritesheet, free_spritesheet (= load_font, free_font) */
#include "bflib_basics.h"   /* ERRORLOG, SYNCLOG */
#include "kfx/profiling/KfxProfiling.h"

#include "post_inc.h"

/******************************************************************************/

FontManager& FontManager::Get()
{
    static FontManager s_instance;
    return s_instance;
}

void FontManager::Register(TbSpriteSheet** slot, const char* name)
{
    for (const auto& e : m_entries)
        if (e.slot == slot) return; // idempotent

    m_entries.push_back({slot, name});
    SYNCLOG("FontManager: registered slot '%s' (%p)", name, (void*)slot);
}

bool FontManager::Load(TbSpriteSheet** slot,
                        const char* dat_path, const char* tab_path)
{
    KFX_ZONE_COLOR("FontMgr::Load", KFX_COLOR_RENDER_CPU);

    if (*slot)
        free_spritesheet(slot); // free_font is #define free_spritesheet

    *slot = load_spritesheet(dat_path, tab_path); // load_font is #define load_spritesheet
    BumpGeneration(); // always — even on failure, stale commands must be dropped

    if (!*slot) {
        ERRORLOG("FontManager: failed to load '%s'", dat_path);
        return false;
    }
    SYNCLOG("FontManager: loaded '%s' into slot %p", dat_path, (void*)slot);
    return true;
}

void FontManager::Free(TbSpriteSheet** slot)
{
    KFX_ZONE_COLOR("FontMgr::Free", KFX_COLOR_RENDER_CPU);

    if (!*slot) return;
    free_spritesheet(slot);
    BumpGeneration();
    SYNCLOG("FontManager: freed slot %p", (void*)slot);
}

void FontManager::FreeAll()
{
    KFX_ZONE_COLOR("FontMgr::FreeAll", KFX_COLOR_RENDER_CPU);

    for (const auto& e : m_entries) {
        if (*e.slot) {
            free_spritesheet(e.slot);
            SYNCLOG("FontManager: freed '%s' in FreeAll", e.name);
        }
    }
    BumpGeneration();
}

void FontManager::BumpGeneration()
{
    uint32_t gen = m_generation.fetch_add(1, std::memory_order_relaxed) + 1;
    KFX_PLOT("FontGeneration", (int64_t)gen);
}

/******************************************************************************/
// C shims

extern "C" {

TbBool FontMgr_Load(TbSpriteSheet** slot,
                     const char* dat_path, const char* tab_path)
{
    return FontManager::Get().Load(slot, dat_path, tab_path) ? 1 : 0;
}

void FontMgr_Free(TbSpriteSheet** slot)
{
    FontManager::Get().Free(slot);
}

void FontMgr_BumpGeneration(void)
{
    FontManager::Get().BumpGeneration();
}

uint32_t FontMgr_GetGeneration(void)
{
    return FontManager::Get().GetGeneration();
}

} // extern "C"
