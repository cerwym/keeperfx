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

/** Reset the recorded ZIP list; call at the start of init_custom_sprites. */
void sprite_cache_reset_zips(void);

/** Record that a ZIP was opened for sprite loading (call in load_file_sprites). */
void sprite_cache_record_zip(const char *path);

/**
 * Try to load sprite data from the binary cache for the given level.
 * Verifies ZIP fingerprints (mtime + size) before accepting the cache.
 * Returns true if the cache was valid and all data has been loaded into ctx.
 * Returns false if the cache is missing, stale, or corrupt.
 */
TbBool sprite_cache_try_load(LevelNumber lvnum, SpriteCacheCtx *ctx);

/**
 * Write current sprite data to the binary cache for the given level.
 * Call after all ZIPs have been loaded successfully.
 * The list of ZIPs is taken from sprite_cache_record_zip calls made during
 * this load cycle.
 */
void sprite_cache_write(LevelNumber lvnum, const SpriteCacheCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_VITA */
#endif /* CUSTOM_SPRITES_CACHE_H */
