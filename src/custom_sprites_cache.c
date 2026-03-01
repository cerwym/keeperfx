#ifdef PLATFORM_VITA

#include "pre_inc.h"
#include "custom_sprites_cache.h"
#include "bflib_sprite.h"
#include "bflib_fileio.h"
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <SDL2/SDL.h>
#include <string.h>
#include <stdlib.h>
#include "post_inc.h"

/* -------------------------------------------------------------------------
 * File format constants
 * -------------------------------------------------------------------------
 *  Header (32 bytes)
 *  ZIP fingerprints (variable)
 *  creature_table_add  — KeeperSprite[num_ksprites], raw
 *  keepersprite_add    — pixel blobs, size = (SWidth+2)*(SHeight+3) each
 *  iso_td_add          — short[KEEPERSPRITE_ADD_NUM], raw
 *  td_iso_add          — short[KEEPERSPRITE_ADD_NUM], raw
 *  added_sprites names — [uint16 len, char[] name, int32 num] x num_names
 *  custom_sprites      — [uint8 w, uint8 h, uint8[] pixels] x num_cspr
 *  added_icons names   — [uint16 len, char[] name, int32 num] x num_icons
 * ------------------------------------------------------------------------- */
#define CACHE_MAGIC   0x4B465843u /* 'KFXC' */
#define CACHE_VERSION 1u
#define MAX_ZIP_FPS   64
#define CACHE_DIR     "ux0:data/keeperfx/cache"
#define MAX_PATH_LEN  256

/* Per-ZIP fingerprint (collected during normal load) */
typedef struct {
    char     path[MAX_PATH_LEN];
    uint32_t file_size;
    uint16_t mtime_year;
    uint8_t  mtime_month;
    uint8_t  mtime_day;
    uint8_t  mtime_hour;
    uint8_t  mtime_minute;
    uint8_t  mtime_second;
} ZipFP;

static ZipFP s_fps[MAX_ZIP_FPS];
static int   s_num_fps = 0;

/* -------------------------------------------------------------------------
 * Public: ZIP tracking
 * ------------------------------------------------------------------------- */

void sprite_cache_reset_zips(void)
{
    s_num_fps = 0;
}

void sprite_cache_record_zip(const char *path)
{
    if (s_num_fps >= MAX_ZIP_FPS) {
        WARNLOG("sprite_cache: too many ZIPs, fingerprint table full");
        return;
    }
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0)
        return; /* can't stat — skip */
    ZipFP *fp = &s_fps[s_num_fps++];
    strncpy(fp->path, path, MAX_PATH_LEN - 1);
    fp->path[MAX_PATH_LEN - 1] = '\0';
    fp->file_size    = (uint32_t)st.st_size;
    fp->mtime_year   = st.st_mtime.year;
    fp->mtime_month  = st.st_mtime.month;
    fp->mtime_day    = st.st_mtime.day;
    fp->mtime_hour   = st.st_mtime.hour;
    fp->mtime_minute = st.st_mtime.minute;
    fp->mtime_second = st.st_mtime.second;
}

/* -------------------------------------------------------------------------
 * I/O helpers — all return 0 on success, non-zero on error
 * ------------------------------------------------------------------------- */

static int iow_u8 (SceUID fd, uint8_t  v) { return sceIoWrite(fd, &v, 1) != 1; }
static int iow_u16(SceUID fd, uint16_t v) { return sceIoWrite(fd, &v, 2) != 2; }
static int iow_u32(SceUID fd, uint32_t v) { return sceIoWrite(fd, &v, 4) != 4; }
static int iow_i32(SceUID fd, int32_t  v) { return sceIoWrite(fd, &v, 4) != 4; }
static int iow_buf(SceUID fd, const void *p, int n) { return sceIoWrite(fd, p, n) != n; }

static int ior_u8 (SceUID fd, uint8_t  *v) { return sceIoRead(fd, v, 1) != 1; }
static int ior_u16(SceUID fd, uint16_t *v) { return sceIoRead(fd, v, 2) != 2; }
static int ior_u32(SceUID fd, uint32_t *v) { return sceIoRead(fd, v, 4) != 4; }
static int ior_i32(SceUID fd, int32_t  *v) { return sceIoRead(fd, v, 4) != 4; }
static int ior_buf(SceUID fd, void *p, int n) { return sceIoRead(fd, p, n) != n; }

/* -------------------------------------------------------------------------
 * Cache path helper
 * ------------------------------------------------------------------------- */

static void make_cache_path(char *buf, size_t bufsz, LevelNumber lvnum)
{
    snprintf(buf, bufsz, CACHE_DIR "/sprites_%05lu.bin", (unsigned long)lvnum);
}

static void make_tmp_path(char *buf, size_t bufsz, LevelNumber lvnum)
{
    snprintf(buf, bufsz, CACHE_DIR "/sprites_%05lu.bin.tmp", (unsigned long)lvnum);
}

/* -------------------------------------------------------------------------
 * Cache write
 * ------------------------------------------------------------------------- */

void sprite_cache_write(LevelNumber lvnum, const SpriteCacheCtx *ctx)
{
    uint32_t t0 = SDL_GetTicks();

    /* Ensure cache directory exists */
    sceIoMkdir(CACHE_DIR, 0777); /* ignore error — may already exist */

    char tmp_path[MAX_PATH_LEN], final_path[MAX_PATH_LEN];
    make_tmp_path(tmp_path, sizeof(tmp_path), lvnum);
    make_cache_path(final_path, sizeof(final_path), lvnum);

    SceUID fd = sceIoOpen(tmp_path,
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        WARNLOG("sprite_cache: cannot open '%s' for writing (0x%x)", tmp_path, fd);
        return;
    }

    uint32_t num_ksprites = (uint32_t)*ctx->p_next_free_sprite;
    uint32_t num_names    = (uint32_t)*ctx->p_num_added_sprite;
    uint32_t num_cspr     = (uint32_t)num_sprites(*ctx->pp_custom_sprites);
    uint32_t num_icons    = (uint32_t)*ctx->p_num_added_icons;
    uint32_t nfi          = (uint32_t)*ctx->p_next_free_icon;

    /* Header */
    if (iow_u32(fd, CACHE_MAGIC)   ||
        iow_u32(fd, CACHE_VERSION) ||
        iow_u32(fd, (uint32_t)s_num_fps) ||
        iow_u32(fd, num_ksprites)  ||
        iow_u32(fd, num_names)     ||
        iow_u32(fd, num_cspr)      ||
        iow_u32(fd, num_icons)     ||
        iow_u32(fd, nfi))
        goto err;

    /* ZIP fingerprints */
    for (int i = 0; i < s_num_fps; i++) {
        const ZipFP *fp = &s_fps[i];
        uint16_t plen = (uint16_t)strlen(fp->path);
        if (iow_u16(fd, plen) ||
            iow_buf(fd, fp->path, plen) ||
            iow_u32(fd, fp->file_size) ||
            iow_u16(fd, fp->mtime_year) ||
            iow_u8 (fd, fp->mtime_month) ||
            iow_u8 (fd, fp->mtime_day) ||
            iow_u8 (fd, fp->mtime_hour) ||
            iow_u8 (fd, fp->mtime_minute) ||
            iow_u8 (fd, fp->mtime_second))
            goto err;
    }

    /* creature_table_add — raw dump */
    if (iow_buf(fd, ctx->creature_table_add,
                num_ksprites * sizeof(struct KeeperSprite)))
        goto err;

    /* keepersprite_add pixel blobs */
    for (uint32_t i = 0; i < num_ksprites; i++) {
        int w  = ctx->creature_table_add[i].SWidth;
        int h  = ctx->creature_table_add[i].SHeight;
        int sz = (w + 2) * (h + 3);
        if (iow_buf(fd, ctx->keepersprite_add[i], sz))
            goto err;
    }

    /* iso_td_add / td_iso_add */
    if (iow_buf(fd, ctx->iso_td_add,
                KEEPERSPRITE_ADD_NUM * sizeof(short)) ||
        iow_buf(fd, ctx->td_iso_add,
                KEEPERSPRITE_ADD_NUM * sizeof(short)))
        goto err;

    /* added_sprites names */
    for (uint32_t i = 0; i < num_names; i++) {
        const char *name = ctx->added_sprites[i].name;
        uint16_t nlen = name ? (uint16_t)strlen(name) : 0;
        if (iow_u16(fd, nlen) ||
            (nlen && iow_buf(fd, name, nlen)) ||
            iow_i32(fd, (int32_t)ctx->added_sprites[i].num))
            goto err;
    }

    /* custom_sprites pixel blobs */
    for (uint32_t i = 0; i < num_cspr; i++) {
        const struct TbSprite *spr = get_sprite(*ctx->pp_custom_sprites, i);
        if (!spr) goto err;
        uint8_t w = spr->SWidth;
        uint8_t h = spr->SHeight;
        int sz = (w + 2) * (h + 3);
        if (iow_u8(fd, w) ||
            iow_u8(fd, h) ||
            iow_buf(fd, spr->Data, sz))
            goto err;
    }

    /* added_icons names */
    for (uint32_t i = 0; i < num_icons; i++) {
        const char *name = ctx->added_icons[i].name;
        uint16_t nlen = name ? (uint16_t)strlen(name) : 0;
        if (iow_u16(fd, nlen) ||
            (nlen && iow_buf(fd, name, nlen)) ||
            iow_i32(fd, (int32_t)ctx->added_icons[i].num))
            goto err;
    }

    sceIoClose(fd);

    /* Atomic rename: delete old, rename tmp */
    sceIoRemove(final_path);
    if (sceIoRename(tmp_path, final_path) < 0) {
        WARNLOG("sprite_cache: rename to '%s' failed", final_path);
        sceIoRemove(tmp_path);
        return;
    }

    JUSTLOG("sprite_cache: wrote '%s' (%u sprites, %u icons) in %u ms",
            final_path, num_ksprites, num_cspr, SDL_GetTicks() - t0);
    return;

err:
    WARNLOG("sprite_cache: write error — removing '%s'", tmp_path);
    sceIoClose(fd);
    sceIoRemove(tmp_path);
}

/* -------------------------------------------------------------------------
 * Fingerprint verification helper
 * ------------------------------------------------------------------------- */

static TbBool verify_fingerprint(SceUID fd)
{
    uint16_t plen;
    if (ior_u16(fd, &plen) || plen >= MAX_PATH_LEN)
        return false;

    char path[MAX_PATH_LEN];
    if (ior_buf(fd, path, plen))
        return false;
    path[plen] = '\0';

    uint32_t cached_size;
    uint16_t cached_year;
    uint8_t  cached_month, cached_day, cached_hour, cached_minute, cached_second;
    if (ior_u32(fd, &cached_size) ||
        ior_u16(fd, &cached_year) ||
        ior_u8 (fd, &cached_month) ||
        ior_u8 (fd, &cached_day) ||
        ior_u8 (fd, &cached_hour) ||
        ior_u8 (fd, &cached_minute) ||
        ior_u8 (fd, &cached_second))
        return false;

    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0)
        return false; /* file gone — cache stale */

    return (uint32_t)st.st_size      == cached_size    &&
           st.st_mtime.year          == cached_year    &&
           st.st_mtime.month         == cached_month   &&
           st.st_mtime.day           == cached_day     &&
           st.st_mtime.hour          == cached_hour    &&
           st.st_mtime.minute        == cached_minute  &&
           st.st_mtime.second        == cached_second;
}

/* -------------------------------------------------------------------------
 * Cache load
 * ------------------------------------------------------------------------- */

TbBool sprite_cache_try_load(LevelNumber lvnum, SpriteCacheCtx *ctx)
{
    uint32_t t0 = SDL_GetTicks();

    char cache_path[MAX_PATH_LEN];
    make_cache_path(cache_path, sizeof(cache_path), lvnum);

    SceUID fd = sceIoOpen(cache_path, SCE_O_RDONLY, 0);
    if (fd < 0)
        return false; /* no cache file yet */

    /* Read and verify header */
    uint32_t magic, version, num_zips;
    uint32_t num_ksprites, num_names, num_cspr, num_icons, nfi;
    if (ior_u32(fd, &magic)        || magic   != CACHE_MAGIC   ||
        ior_u32(fd, &version)      || version != CACHE_VERSION ||
        ior_u32(fd, &num_zips)     ||
        ior_u32(fd, &num_ksprites) ||
        ior_u32(fd, &num_names)    ||
        ior_u32(fd, &num_cspr)     ||
        ior_u32(fd, &num_icons)    ||
        ior_u32(fd, &nfi))
        goto stale;

    /* Sanity-check counts */
    if (num_ksprites > (uint32_t)KEEPERSPRITE_ADD_NUM ||
        num_names    > (uint32_t)KEEPERSPRITE_ADD_NUM ||
        num_zips     > MAX_ZIP_FPS)
        goto stale;

    /* Verify fingerprints */
    uint32_t t_fp = SDL_GetTicks();
    for (uint32_t i = 0; i < num_zips; i++) {
        if (!verify_fingerprint(fd))
            goto stale;
    }
    JUSTLOG("sprite_cache: fingerprints OK (%u ZIPs) in %u ms",
            num_zips, SDL_GetTicks() - t_fp);

    /* creature_table_add — raw load */
    if (ior_buf(fd, ctx->creature_table_add,
                num_ksprites * sizeof(struct KeeperSprite)))
        goto stale;

    /* keepersprite_add pixel blobs */
    for (uint32_t i = 0; i < num_ksprites; i++) {
        int w  = ctx->creature_table_add[i].SWidth;
        int h  = ctx->creature_table_add[i].SHeight;
        int sz = (w + 2) * (h + 3);
        ctx->keepersprite_add[i] = malloc(sz);
        if (!ctx->keepersprite_add[i])
            goto cleanup;
        if (ior_buf(fd, ctx->keepersprite_add[i], sz))
            goto cleanup;
    }
    *ctx->p_next_free_sprite = (short)num_ksprites;

    /* iso_td_add / td_iso_add */
    if (ior_buf(fd, ctx->iso_td_add,
                KEEPERSPRITE_ADD_NUM * sizeof(short)) ||
        ior_buf(fd, ctx->td_iso_add,
                KEEPERSPRITE_ADD_NUM * sizeof(short)))
        goto cleanup;

    /* added_sprites names */
    for (uint32_t i = 0; i < num_names; i++) {
        uint16_t nlen;
        int32_t  num;
        if (ior_u16(fd, &nlen) || nlen >= 256)
            goto cleanup;
        char *name = malloc(nlen + 1);
        if (!name) goto cleanup;
        if (nlen && ior_buf(fd, name, nlen)) { free(name); goto cleanup; }
        name[nlen] = '\0';
        ctx->added_sprites[i].name = name;
        if (ior_i32(fd, &num)) goto cleanup;
        ctx->added_sprites[i].num = num;
    }
    *ctx->p_num_added_sprite = (int)num_names;

    /* custom_sprites pixel blobs */
    for (uint32_t i = 0; i < num_cspr; i++) {
        uint8_t w, h;
        if (ior_u8(fd, &w) || ior_u8(fd, &h))
            goto cleanup;
        int sz = (w + 2) * (h + 3);
        unsigned char *data = malloc(sz);
        if (!data) goto cleanup;
        if (ior_buf(fd, data, sz)) { free(data); goto cleanup; }
        TbBool ok = add_sprite(*ctx->pp_custom_sprites, w, h, sz, data);
        free(data);
        if (!ok) goto cleanup;
    }

    /* added_icons names */
    for (uint32_t i = 0; i < num_icons; i++) {
        uint16_t nlen;
        int32_t  num;
        if (ior_u16(fd, &nlen) || nlen >= 256)
            goto cleanup;
        char *name = malloc(nlen + 1);
        if (!name) goto cleanup;
        if (nlen && ior_buf(fd, name, nlen)) { free(name); goto cleanup; }
        name[nlen] = '\0';
        ctx->added_icons[i].name = name;
        if (ior_i32(fd, &num)) goto cleanup;
        ctx->added_icons[i].num = num;
    }
    *ctx->p_num_added_icons  = (int)num_icons;
    *ctx->p_next_free_icon   = (short)nfi;

    sceIoClose(fd);
    JUSTLOG("sprite_cache: loaded '%s' (%u sprites, %u icons) in %u ms",
            cache_path, num_ksprites, num_cspr, SDL_GetTicks() - t0);
    return true;

stale:
    sceIoClose(fd);
    return false;

cleanup:
    /* Partial load failed — free anything we allocated so the caller can
     * fall through to the normal load path cleanly. */
    for (int i = 0; i < KEEPERSPRITE_ADD_NUM; i++) {
        if (ctx->keepersprite_add[i]) {
            free(ctx->keepersprite_add[i]);
            ctx->keepersprite_add[i] = NULL;
        }
    }
    *ctx->p_next_free_sprite = 0;
    for (int i = 0; i < *ctx->p_num_added_sprite; i++) {
        if (ctx->added_sprites[i].name) {
            free((char *)ctx->added_sprites[i].name);
            ctx->added_sprites[i].name = NULL;
        }
    }
    *ctx->p_num_added_sprite = 0;
    for (int i = 0; i < *ctx->p_num_added_icons; i++) {
        if (ctx->added_icons[i].name) {
            free((char *)ctx->added_icons[i].name);
            ctx->added_icons[i].name = NULL;
        }
    }
    *ctx->p_num_added_icons = 0;
    *ctx->p_next_free_icon  = 0;
    sceIoClose(fd);
    WARNLOG("sprite_cache: load failed mid-way — falling back to ZIP loading");
    return false;
}

#endif /* PLATFORM_VITA */
