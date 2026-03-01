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
 * File format (version 2)
 *
 *  Header (52 bytes):
 *    magic        (4)  = 'KFXC'
 *    version      (4)  = 2
 *    base_kspr    (4)  start ksprite index for this tier
 *    cnt_kspr     (4)  number of ksprites in this tier's delta
 *    base_names   (4)  start added_sprites index
 *    cnt_names    (4)  count of added_sprites names in delta
 *    base_cspr    (4)  start custom_sprites index
 *    cnt_cspr     (4)  count of custom_sprites in delta
 *    base_icons   (4)  start added_icons index
 *    cnt_icons    (4)  count of added_icons in delta
 *    base_nfi     (4)  next_free_icon at tier start
 *    end_nfi      (4)  next_free_icon at tier end
 *    n_zips       (4)  number of ZIP fingerprints
 *
 *  ZIP fingerprints [n_zips x variable]
 *  creature_table_add delta  — KeeperSprite[cnt_kspr], raw
 *  keepersprite_add blobs    — variable per sprite
 *  iso_td_add delta          — short[cnt_kspr]
 *  td_iso_add delta          — short[cnt_kspr]
 *  added_sprites names delta — [u16 len, chars, i32 num] x cnt_names
 *  custom_sprites blobs      — [u8 w, u8 h, pixels] x cnt_cspr
 *  added_icons names delta   — [u16 len, chars, i32 num] x cnt_icons
 * ------------------------------------------------------------------------- */
#define CACHE_MAGIC   0x4B465843u   /* 'KFXC' */
#define CACHE_VERSION 2u
#define MAX_ZIP_FPS   64
#define CACHE_DIR     "ux0:data/keeperfx/cache"
#define MAX_PATH_LEN  256

typedef struct {
    char     path[MAX_PATH_LEN];
    uint32_t file_size;
    uint16_t mtime_year;
    uint8_t  mtime_month, mtime_day, mtime_hour, mtime_minute, mtime_second;
} ZipFP;

static ZipFP s_phase_fps[3][MAX_ZIP_FPS];
static int   s_phase_n_fps[3];
static int   s_cur_phase = 0;

/* -------------------------------------------------------------------------
 * Public: phase / ZIP tracking
 * ------------------------------------------------------------------------- */

void sprite_cache_begin_phase(int phase)
{
    s_cur_phase = phase;
    s_phase_n_fps[phase] = 0;
}

void sprite_cache_record_zip(const char *path)
{
    int phase = s_cur_phase;
    if (s_phase_n_fps[phase] >= MAX_ZIP_FPS) {
        WARNLOG("sprite_cache: phase %d fingerprint table full", phase);
        return;
    }
    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0)
        return;
    ZipFP *fp = &s_phase_fps[phase][s_phase_n_fps[phase]++];
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
 * Public: snapshot
 * ------------------------------------------------------------------------- */

void sprite_cache_snapshot(const SpriteCacheCtx *ctx, SpriteCacheTierSnapshot *snap)
{
    snap->next_free_sprite   = (int)*ctx->p_next_free_sprite;
    snap->num_added_sprite   = *ctx->p_num_added_sprite;
    snap->next_free_icon     = (int)*ctx->p_next_free_icon;
    snap->num_added_icons    = *ctx->p_num_added_icons;
    snap->num_custom_sprites = (int)num_sprites(*ctx->pp_custom_sprites);
}

/* -------------------------------------------------------------------------
 * I/O helpers
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
 * Cache path helpers
 * ------------------------------------------------------------------------- */

static uint32_t djb2_hash(const char *s)
{
    uint32_t h = 5381;
    unsigned char c;
    while ((c = (unsigned char)*s++) != '\0')
        h = h * 33 ^ c;
    return h;
}

static void make_path_global(char *buf, size_t sz)
{
    snprintf(buf, sz, CACHE_DIR "/sprites_global.bin");
}

static void make_path_campaign(char *buf, size_t sz, const char *cmpg_dir)
{
    snprintf(buf, sz, CACHE_DIR "/sprites_campgn_%08x.bin", djb2_hash(cmpg_dir));
}

static void make_path_level(char *buf, size_t sz, const char *level_zip)
{
    snprintf(buf, sz, CACHE_DIR "/sprites_level_%08x.bin", djb2_hash(level_zip));
}

/* -------------------------------------------------------------------------
 * Fingerprint I/O
 * ------------------------------------------------------------------------- */

static int write_fingerprints(SceUID fd, const ZipFP *fps, int n)
{
    for (int i = 0; i < n; i++) {
        uint16_t plen = (uint16_t)strlen(fps[i].path);
        if (iow_u16(fd, plen) || iow_buf(fd, fps[i].path, plen) ||
            iow_u32(fd, fps[i].file_size) ||
            iow_u16(fd, fps[i].mtime_year) ||
            iow_u8 (fd, fps[i].mtime_month) ||
            iow_u8 (fd, fps[i].mtime_day) ||
            iow_u8 (fd, fps[i].mtime_hour) ||
            iow_u8 (fd, fps[i].mtime_minute) ||
            iow_u8 (fd, fps[i].mtime_second))
            return -1;
    }
    return 0;
}

static TbBool verify_fingerprint_one(SceUID fd)
{
    uint16_t plen;
    if (ior_u16(fd, &plen) || plen >= MAX_PATH_LEN)
        return false;
    char path[MAX_PATH_LEN];
    if (ior_buf(fd, path, plen)) return false;
    path[plen] = '\0';

    uint32_t cached_size;
    uint16_t cached_year;
    uint8_t  cached_month, cached_day, cached_hour, cached_minute, cached_second;
    if (ior_u32(fd, &cached_size) || ior_u16(fd, &cached_year) ||
        ior_u8(fd, &cached_month) || ior_u8(fd, &cached_day) ||
        ior_u8(fd, &cached_hour) || ior_u8(fd, &cached_minute) ||
        ior_u8(fd, &cached_second))
        return false;

    SceIoStat st;
    if (sceIoGetstat(path, &st) < 0)
        return false;
    return (uint32_t)st.st_size == cached_size &&
           st.st_mtime.year     == cached_year  &&
           st.st_mtime.month    == cached_month &&
           st.st_mtime.day      == cached_day   &&
           st.st_mtime.hour     == cached_hour  &&
           st.st_mtime.minute   == cached_minute &&
           st.st_mtime.second   == cached_second;
}

/* -------------------------------------------------------------------------
 * Partial-load cleanup: revert state to what it was before this tier loaded
 * ------------------------------------------------------------------------- */

static void revert_to_snapshot(SpriteCacheCtx *ctx, const SpriteCacheTierSnapshot *snap)
{
    int cur_kspr = (int)*ctx->p_next_free_sprite;
    for (int i = snap->next_free_sprite; i < cur_kspr; i++) {
        free(ctx->keepersprite_add[i]);
        ctx->keepersprite_add[i] = NULL;
    }
    *ctx->p_next_free_sprite = (short)snap->next_free_sprite;

    int cur_names = *ctx->p_num_added_sprite;
    for (int i = snap->num_added_sprite; i < cur_names; i++) {
        free((char *)ctx->added_sprites[i].name);
        ctx->added_sprites[i].name = NULL;
    }
    *ctx->p_num_added_sprite = snap->num_added_sprite;

    trim_spritesheet(*ctx->pp_custom_sprites, snap->num_custom_sprites);

    int cur_icons = *ctx->p_num_added_icons;
    for (int i = snap->num_added_icons; i < cur_icons; i++) {
        free((char *)ctx->added_icons[i].name);
        ctx->added_icons[i].name = NULL;
    }
    *ctx->p_num_added_icons = snap->num_added_icons;
    *ctx->p_next_free_icon  = (short)snap->next_free_icon;
}

/* -------------------------------------------------------------------------
 * Common tier write
 * ------------------------------------------------------------------------- */

static void tier_write(const char *path, const SpriteCacheCtx *ctx,
                       const SpriteCacheTierSnapshot *base,
                       const ZipFP *fps, int n_fps)
{
    uint32_t t0 = SDL_GetTicks();
    sceIoMkdir(CACHE_DIR, 0777);

    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    SceUID fd = sceIoOpen(tmp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) {
        WARNLOG("sprite_cache: cannot write '%s' (0x%x)", tmp, fd);
        return;
    }

    int base_kspr  = base->next_free_sprite;
    int base_names = base->num_added_sprite;
    int base_cspr  = base->num_custom_sprites;
    int base_icons = base->num_added_icons;
    int base_nfi   = base->next_free_icon;

    int end_kspr   = (int)*ctx->p_next_free_sprite;
    int end_names  = *ctx->p_num_added_sprite;
    int end_cspr   = (int)num_sprites(*ctx->pp_custom_sprites);
    int end_icons  = *ctx->p_num_added_icons;
    int end_nfi    = (int)*ctx->p_next_free_icon;

    int cnt_kspr  = end_kspr  - base_kspr;
    int cnt_names = end_names - base_names;
    int cnt_cspr  = end_cspr  - base_cspr;
    int cnt_icons = end_icons - base_icons;

    /* Header */
    if (iow_u32(fd, CACHE_MAGIC)          ||
        iow_u32(fd, CACHE_VERSION)        ||
        iow_u32(fd, (uint32_t)base_kspr)  ||
        iow_u32(fd, (uint32_t)cnt_kspr)   ||
        iow_u32(fd, (uint32_t)base_names) ||
        iow_u32(fd, (uint32_t)cnt_names)  ||
        iow_u32(fd, (uint32_t)base_cspr)  ||
        iow_u32(fd, (uint32_t)cnt_cspr)   ||
        iow_u32(fd, (uint32_t)base_icons) ||
        iow_u32(fd, (uint32_t)cnt_icons)  ||
        iow_u32(fd, (uint32_t)base_nfi)   ||
        iow_u32(fd, (uint32_t)end_nfi)    ||
        iow_u32(fd, (uint32_t)n_fps))
        goto err;

    if (write_fingerprints(fd, fps, n_fps) != 0)
        goto err;

    /* creature_table_add delta */
    if (cnt_kspr > 0 &&
        iow_buf(fd, ctx->creature_table_add + base_kspr,
                cnt_kspr * sizeof(struct KeeperSprite)))
        goto err;

    /* keepersprite_add pixel blobs */
    for (int i = base_kspr; i < end_kspr; i++) {
        int w  = ctx->creature_table_add[i].SWidth;
        int h  = ctx->creature_table_add[i].SHeight;
        int sz = (w + 2) * (h + 3);
        if (iow_buf(fd, ctx->keepersprite_add[i], sz))
            goto err;
    }

    /* iso_td_add / td_iso_add delta */
    if (cnt_kspr > 0 &&
        (iow_buf(fd, ctx->iso_td_add + base_kspr, cnt_kspr * sizeof(short)) ||
         iow_buf(fd, ctx->td_iso_add + base_kspr, cnt_kspr * sizeof(short))))
        goto err;

    /* added_sprites names delta */
    for (int i = base_names; i < end_names; i++) {
        const char *name = ctx->added_sprites[i].name;
        uint16_t nlen = name ? (uint16_t)strlen(name) : 0;
        if (iow_u16(fd, nlen) ||
            (nlen && iow_buf(fd, name, nlen)) ||
            iow_i32(fd, (int32_t)ctx->added_sprites[i].num))
            goto err;
    }

    /* custom_sprites pixel blobs delta */
    for (int i = base_cspr; i < end_cspr; i++) {
        const struct TbSprite *spr = get_sprite(*ctx->pp_custom_sprites, i);
        if (!spr) goto err;
        uint8_t w = spr->SWidth, h = spr->SHeight;
        int sz = (w + 2) * (h + 3);
        if (iow_u8(fd, w) || iow_u8(fd, h) || iow_buf(fd, spr->Data, sz))
            goto err;
    }

    /* added_icons names delta */
    for (int i = base_icons; i < end_icons; i++) {
        const char *name = ctx->added_icons[i].name;
        uint16_t nlen = name ? (uint16_t)strlen(name) : 0;
        if (iow_u16(fd, nlen) ||
            (nlen && iow_buf(fd, name, nlen)) ||
            iow_i32(fd, (int32_t)ctx->added_icons[i].num))
            goto err;
    }

    sceIoClose(fd);
    sceIoRemove(path);
    if (sceIoRename(tmp, path) < 0) {
        WARNLOG("sprite_cache: rename failed for '%s'", path);
        sceIoRemove(tmp);
        return;
    }
    JUSTLOG("sprite_cache: wrote '%s' (+%d ksprites, +%d icons) in %u ms",
            path, cnt_kspr, cnt_cspr, SDL_GetTicks() - t0);
    return;

err:
    sceIoClose(fd);
    sceIoRemove(tmp);
    WARNLOG("sprite_cache: write error for '%s'", path);
}

/* -------------------------------------------------------------------------
 * Common tier try_load
 * ------------------------------------------------------------------------- */

static TbBool tier_try_load(const char *path, SpriteCacheCtx *ctx,
                             const SpriteCacheTierSnapshot *expected_base)
{
    uint32_t t0 = SDL_GetTicks();

    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    uint32_t magic, version;
    uint32_t h_base_kspr, h_cnt_kspr, h_base_names, h_cnt_names;
    uint32_t h_base_cspr, h_cnt_cspr, h_base_icons, h_cnt_icons;
    uint32_t h_base_nfi, h_end_nfi, h_n_zips;

    if (ior_u32(fd, &magic)         || magic   != CACHE_MAGIC   ||
        ior_u32(fd, &version)       || version != CACHE_VERSION ||
        ior_u32(fd, &h_base_kspr)   ||
        ior_u32(fd, &h_cnt_kspr)    ||
        ior_u32(fd, &h_base_names)  ||
        ior_u32(fd, &h_cnt_names)   ||
        ior_u32(fd, &h_base_cspr)   ||
        ior_u32(fd, &h_cnt_cspr)    ||
        ior_u32(fd, &h_base_icons)  ||
        ior_u32(fd, &h_cnt_icons)   ||
        ior_u32(fd, &h_base_nfi)    ||
        ior_u32(fd, &h_end_nfi)     ||
        ior_u32(fd, &h_n_zips))
        goto stale;

    /* Validate base matches expected state — catches stale child caches after parent rebuild */
    if ((int)h_base_kspr  != expected_base->next_free_sprite  ||
        (int)h_base_names != expected_base->num_added_sprite  ||
        (int)h_base_cspr  != expected_base->num_custom_sprites ||
        (int)h_base_icons != expected_base->num_added_icons   ||
        (int)h_base_nfi   != expected_base->next_free_icon)
        goto stale;

    if (h_base_kspr + h_cnt_kspr > (uint32_t)KEEPERSPRITE_ADD_NUM ||
        h_n_zips > MAX_ZIP_FPS)
        goto stale;

    for (uint32_t i = 0; i < h_n_zips; i++) {
        if (!verify_fingerprint_one(fd))
            goto stale;
    }

    /* creature_table_add delta */
    if (h_cnt_kspr > 0 &&
        ior_buf(fd, ctx->creature_table_add + h_base_kspr,
                h_cnt_kspr * sizeof(struct KeeperSprite)))
        goto cleanup;

    /* keepersprite_add pixel blobs */
    for (uint32_t i = h_base_kspr; i < h_base_kspr + h_cnt_kspr; i++) {
        int w  = ctx->creature_table_add[i].SWidth;
        int hh = ctx->creature_table_add[i].SHeight;
        int sz = (w + 2) * (hh + 3);
        ctx->keepersprite_add[i] = malloc(sz);
        if (!ctx->keepersprite_add[i]) goto cleanup;
        if (ior_buf(fd, ctx->keepersprite_add[i], sz)) goto cleanup;
    }
    *ctx->p_next_free_sprite = (short)(h_base_kspr + h_cnt_kspr);

    /* iso_td_add / td_iso_add delta */
    if (h_cnt_kspr > 0 &&
        (ior_buf(fd, ctx->iso_td_add + h_base_kspr, h_cnt_kspr * sizeof(short)) ||
         ior_buf(fd, ctx->td_iso_add + h_base_kspr, h_cnt_kspr * sizeof(short))))
        goto cleanup;

    /* added_sprites names delta */
    for (uint32_t i = h_base_names; i < h_base_names + h_cnt_names; i++) {
        uint16_t nlen;
        int32_t  num;
        if (ior_u16(fd, &nlen) || nlen >= 256) goto cleanup;
        char *name = malloc(nlen + 1);
        if (!name) goto cleanup;
        if (nlen && ior_buf(fd, name, nlen)) { free(name); goto cleanup; }
        name[nlen] = '\0';
        ctx->added_sprites[i].name = name;
        if (ior_i32(fd, &num)) goto cleanup;
        ctx->added_sprites[i].num = num;
    }
    *ctx->p_num_added_sprite = (int)(h_base_names + h_cnt_names);

    /* custom_sprites pixel blobs delta */
    for (uint32_t i = 0; i < h_cnt_cspr; i++) {
        uint8_t w, hh;
        if (ior_u8(fd, &w) || ior_u8(fd, &hh)) goto cleanup;
        int sz = (w + 2) * (hh + 3);
        unsigned char *data = malloc(sz);
        if (!data) goto cleanup;
        if (ior_buf(fd, data, sz)) { free(data); goto cleanup; }
        TbBool ok = add_sprite(*ctx->pp_custom_sprites, w, hh, sz, data);
        free(data);
        if (!ok) goto cleanup;
    }

    /* added_icons names delta */
    for (uint32_t i = h_base_icons; i < h_base_icons + h_cnt_icons; i++) {
        uint16_t nlen;
        int32_t  num;
        if (ior_u16(fd, &nlen) || nlen >= 256) goto cleanup;
        char *name = malloc(nlen + 1);
        if (!name) goto cleanup;
        if (nlen && ior_buf(fd, name, nlen)) { free(name); goto cleanup; }
        name[nlen] = '\0';
        ctx->added_icons[i].name = name;
        if (ior_i32(fd, &num)) goto cleanup;
        ctx->added_icons[i].num = num;
    }
    *ctx->p_num_added_icons = (int)(h_base_icons + h_cnt_icons);
    *ctx->p_next_free_icon  = (short)h_end_nfi;

    sceIoClose(fd);
    JUSTLOG("sprite_cache: loaded '%s' (+%u ksprites, +%u icons) in %u ms",
            path, h_cnt_kspr, h_cnt_cspr, SDL_GetTicks() - t0);
    return true;

stale:
    sceIoClose(fd);
    return false;

cleanup:
    sceIoClose(fd);
    revert_to_snapshot(ctx, expected_base);
    WARNLOG("sprite_cache: partial load of '%s' reverted", path);
    return false;
}

/* -------------------------------------------------------------------------
 * Public tier functions
 * ------------------------------------------------------------------------- */

TbBool sprite_cache_try_load_global(SpriteCacheCtx *ctx)
{
    char path[MAX_PATH_LEN];
    make_path_global(path, sizeof(path));
    SpriteCacheTierSnapshot zero = {0,0,0,0,0};
    return tier_try_load(path, ctx, &zero);
}

TbBool sprite_cache_try_load_campaign(const char *cmpg_dir, SpriteCacheCtx *ctx,
                                       const SpriteCacheTierSnapshot *global_snap)
{
    char path[MAX_PATH_LEN];
    make_path_campaign(path, sizeof(path), cmpg_dir);
    return tier_try_load(path, ctx, global_snap);
}

TbBool sprite_cache_try_load_level(const char *level_zip, SpriteCacheCtx *ctx,
                                    const SpriteCacheTierSnapshot *campaign_snap)
{
    char path[MAX_PATH_LEN];
    make_path_level(path, sizeof(path), level_zip);
    return tier_try_load(path, ctx, campaign_snap);
}

void sprite_cache_write_global(SpriteCacheCtx *ctx)
{
    char path[MAX_PATH_LEN];
    make_path_global(path, sizeof(path));
    SpriteCacheTierSnapshot zero = {0,0,0,0,0};
    tier_write(path, ctx, &zero, s_phase_fps[0], s_phase_n_fps[0]);
}

void sprite_cache_write_campaign(const char *cmpg_dir, SpriteCacheCtx *ctx,
                                  const SpriteCacheTierSnapshot *global_snap)
{
    char path[MAX_PATH_LEN];
    make_path_campaign(path, sizeof(path), cmpg_dir);
    tier_write(path, ctx, global_snap, s_phase_fps[1], s_phase_n_fps[1]);
}

void sprite_cache_write_level(const char *level_zip, SpriteCacheCtx *ctx,
                               const SpriteCacheTierSnapshot *campaign_snap)
{
    char path[MAX_PATH_LEN];
    make_path_level(path, sizeof(path), level_zip);
    tier_write(path, ctx, campaign_snap, s_phase_fps[2], s_phase_n_fps[2]);
}

#endif /* PLATFORM_VITA */

