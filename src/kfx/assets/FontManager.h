/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file FontManager.h
 *     Lifecycle manager for all text-font TbSpriteSheet globals.
 *
 *     Fonts are structurally identical to sprite sheets (same TbSpriteSheet*
 *     type) but serve a different purpose: they are rendered via GLTextRenderer's
 *     per-font GLFontAtlas cache, not via the sprite atlas.
 *
 *     Owns the text font generation counter that was previously a raw static in
 *     RendererManager (s_text_font_generation).  The generation is a monotonic
 *     counter snapshotted into every queued IR text command; the render thread
 *     drops any command whose snapshot doesn't match the current value, preventing
 *     use-after-free when fonts are reloaded between queue and execute time.
 *
 *     Managed font globals:
 *       frontend_font[0..3]   (LoadVResMinimal / FreeVResMinimal)
 *       winfont               (LoadVRes256Data / LoadMcgaData and their Free pairs)
 *       font_sprites          (same)
 *       frontstory_font       (front_credits.c load / free)
 *
 *     Contract:
 *       - Register() every font slot once at startup (idempotent).
 *       - Use Load() / Free() instead of load_font() / free_font() directly.
 *       - All calls must be on the game thread.
 */
/******************************************************************************/
#pragma once

#include "bflib_sprite.h"   /* TbSpriteSheet */
#include "bflib_basics.h"   /* TbBool */
#include <stdint.h>

#ifdef __cplusplus
#include <atomic>
#include <vector>

class FontManager {
public:
    static FontManager& Get();

    /** Register a C global font slot.  Idempotent. */
    void Register(TbSpriteSheet** slot, const char* name);

    /** Free existing font, load new one, bump generation.
     *  Returns true on success; slot is NULL on failure. */
    bool Load(TbSpriteSheet** slot, const char* dat_path, const char* tab_path);

    /** Free slot and bump generation.  No-op when slot is NULL. */
    void Free(TbSpriteSheet** slot);

    /** Free ALL registered font slots and bump generation once. */
    void FreeAll();

    /** Explicitly bump the generation (e.g. on renderer shutdown). */
    void BumpGeneration();

    /** Current generation value — snapshot into queued text commands. */
    uint32_t GetGeneration() const { return m_generation.load(std::memory_order_relaxed); }

private:
    struct Entry { TbSpriteSheet** slot; const char* name; };

    std::vector<Entry>    m_entries;
    std::atomic<uint32_t> m_generation {1};  // 0 is reserved as "always stale"
};

#endif // __cplusplus

// ── C-callable shims for .c translation units ────────────────────────────────

#ifdef __cplusplus
extern "C" {
#endif

TbBool   FontMgr_Load(struct TbSpriteSheet** slot,
                      const char* dat_path, const char* tab_path);
void     FontMgr_Free(struct TbSpriteSheet** slot);
void     FontMgr_BumpGeneration(void);
uint32_t FontMgr_GetGeneration(void);

#ifdef __cplusplus
}
#endif
