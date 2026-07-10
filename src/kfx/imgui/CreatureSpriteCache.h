/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CreatureSpriteCache.h
 *     Self-contained CPU cache of decoded creature (keeper) sprite frames for
 *     the ImGui debug viewer.
 *
 *     The cache is fully decoupled from the game's sprite heap and renderer.
 *     It opens its OWN private handle to data/creature.jty and decodes every
 *     frame's RLE into palette-index pixels, driven purely by the read-only
 *     (static-after-load) creature graphics tables. It NEVER touches the LRU
 *     sprite heap, keepsprite[], or the shared jty_file_handle, so populating
 *     it has zero effect on game state — a creature need not be on screen.
 *
 *     Threading:
 *       - CreatureSpriteCache_RequestLoad() : call from the render/UI thread
 *         (e.g. while the Creatures tab is visible). Cheap, non-blocking.
 *       - CreatureSpriteCache_Service()     : call once per frame on the GAME
 *         thread (from redraw_display). Performs the actual disk read/decode
 *         when a load is pending and the creature data is available.
 *       - CreatureSpriteCache_Invalidate()  : call when the creature data set
 *         changes (level load) to force a reload on the next request.
 *       - CreatureSpriteCache_Get*          : safe to call from any thread.
 */
/******************************************************************************/
#ifndef KEEPERFX_DEBUG_CREATURE_SPRITE_CACHE_H
#define KEEPERFX_DEBUG_CREATURE_SPRITE_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Request a (re)load of the creature sprite cache. Serviced on the game thread.
 *  No-op if the cache is already current. Safe to call every frame. */
void CreatureSpriteCache_RequestLoad(void);

/** Service a pending load. MUST be called on the game thread (reads engine
 *  statics and does file I/O). Cheap when no load is pending. */
void CreatureSpriteCache_Service(void);

/** Mark the cached data stale (e.g. on level load). The next RequestLoad that
 *  is serviced will rebuild the cache. */
void CreatureSpriteCache_Invalidate(void);

/** Monotonic counter bumped whenever the cache contents change. The viewer uses
 *  this to know when to drop and rebuild its GL textures. */
int CreatureSpriteCache_GetGeneration(void);

/** Number of decoded frames currently cached. */
int CreatureSpriteCache_GetCount(void);

/** Fetch one cached frame by engine keeper-sprite index.
 *  Writes the frame's width/height to out_w/out_h (when non-NULL) and, when
 *  out_pixels is non-NULL and out_cap is large enough, copies w*h palette-index
 *  bytes into out_pixels. Pass out_pixels=NULL to query dimensions only.
 *  Returns 1 if the frame exists (and, when requested, was copied), else 0. */
int CreatureSpriteCache_GetFrame(int kspr_idx, unsigned char* out_pixels,
                                 int out_cap, int* out_w, int* out_h);

#ifdef __cplusplus
}
#endif

#endif // KEEPERFX_DEBUG_CREATURE_SPRITE_CACHE_H
