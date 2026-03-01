#ifndef CUSTOM_SPRITES_CACHE_H
#define CUSTOM_SPRITES_CACHE_H

#ifdef PLATFORM_VITA

#include "globals.h"
#include "bflib_sprite.h"
#include "creature_graphics.h"
#include "engine_render.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pointers into custom_sprites.c module statics, used by the cache read/write
 * functions without exposing those statics through extern linkage.
 */
typedef struct SpriteCacheCtx {
    TbSpriteData          *keepersprite_add;   /* [KEEPERSPRITE_ADD_NUM] */
    struct KeeperSprite   *creature_table_add; /* [KEEPERSPRITE_ADD_NUM] */
    short                 *iso_td_add;         /* [KEEPERSPRITE_ADD_NUM] */
    short                 *td_iso_add;         /* [KEEPERSPRITE_ADD_NUM] */
    short                 *p_next_free_sprite;
    struct NamedCommand   *added_sprites;      /* [KEEPERSPRITE_ADD_NUM] */
    int                   *p_num_added_sprite;
    struct TbSpriteSheet **pp_custom_sprites;
    struct NamedCommand   *added_icons;        /* [GUI_PANEL_SPRITES_NEW] */
    int                   *p_num_added_icons;
    short                 *p_next_free_icon;
} SpriteCacheCtx;

/** Counter state captured at a tier boundary; used as the "start" for the next tier's delta. */
typedef struct SpriteCacheTierSnapshot {
    int next_free_sprite;
    int num_added_sprite;
    int next_free_icon;
    int num_added_icons;
    int num_custom_sprites;
} SpriteCacheTierSnapshot;

/** Call before loading each tier's ZIPs to reset that phase's fingerprint accumulator. */
void sprite_cache_begin_phase(int phase);  /* 0=global, 1=campaign, 2=level */

/** Record a ZIP opened for sprite loading (called from load_file_sprites). */
void sprite_cache_record_zip(const char *path);

/** Capture a snapshot of the current counter state. */
void sprite_cache_snapshot(const SpriteCacheCtx *ctx, SpriteCacheTierSnapshot *snap);

/* Load functions: return true on cache hit, false on miss/stale. */
TbBool sprite_cache_try_load_global(SpriteCacheCtx *ctx);
TbBool sprite_cache_try_load_campaign(const char *cmpg_dir, SpriteCacheCtx *ctx,
                                       const SpriteCacheTierSnapshot *global_snap);
TbBool sprite_cache_try_load_level(const char *level_zip, SpriteCacheCtx *ctx,
                                    const SpriteCacheTierSnapshot *campaign_snap);

/* Write functions: serialise only the delta since base_snap. */
void sprite_cache_write_global(SpriteCacheCtx *ctx);
void sprite_cache_write_campaign(const char *cmpg_dir, SpriteCacheCtx *ctx,
                                  const SpriteCacheTierSnapshot *global_snap);
void sprite_cache_write_level(const char *level_zip, SpriteCacheCtx *ctx,
                               const SpriteCacheTierSnapshot *campaign_snap);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_VITA */
#endif /* CUSTOM_SPRITES_CACHE_H */
