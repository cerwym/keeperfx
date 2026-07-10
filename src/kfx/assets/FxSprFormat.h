/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprFormat.h
 *     On-disk layout of the `.fxspr` truecolour sprite container (v1).
 *
 *     `.fxspr` is the canonical-RGBA sprite container described in
 *     docs/sprite-asset-unification.md (section 5). It replaces the split
 *     `.dat` + `.tab` (+ `.pal`) representation with one self-describing,
 *     little-endian artifact carrying straight RGBA8 pixels.
 *
 *     This header is the SINGLE SOURCE OF TRUTH for the byte layout: both the
 *     engine loader and the offline transcoder/packer must agree with it.
 *
 *     v1 scope (bootstrap): `Image` / `SpriteSheet` kinds only, no optional
 *     palette / animation / indexed-cache blocks. Those slot in later behind
 *     header flags + offsets without breaking existing readers.
 *
 *     PARITY CONTRACT: entry order and per-entry width/height must equal the
 *     indexed `.tab` for the same asset, so a SpriteHandle maps 1:1 across the
 *     indexed and truecolour catalogues. Entry 0 mirrors the `.tab` null
 *     sentinel (width==height==0, zero-size payload).
 */
/******************************************************************************/
#ifndef KEEPERFX_KFX_ASSETS_FXSPRFORMAT_H
#define KEEPERFX_KFX_ASSETS_FXSPRFORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Magic bytes at file offset 0: 'F','X','S','P'. */
#define FXSPR_MAGIC0 'F'
#define FXSPR_MAGIC1 'X'
#define FXSPR_MAGIC2 'S'
#define FXSPR_MAGIC3 'P'

/** Current container version written/expected by this build. */
#define FXSPR_VERSION 1u

/** Header flag bits. v1 bootstrap writes flags == 0. */
enum FxSprFlags {
    FxSprFlag_PremultipliedAlpha = 0x0001, /**< bit0: 0=straight, 1=premultiplied */
    FxSprFlag_Dims16             = 0x0002, /**< bit1: dims are 16-bit (always set in v1) */
    FxSprFlag_PayloadCompressed  = 0x0004, /**< bit2: payload zlib/RNC compressed (unused in v1) */
    FxSprFlag_IndexedCache       = 0x0008, /**< bit3: precomputed R8+RLE block present (unused in v1) */
};

/** Asset kind discriminator. One container, not sub-formats. */
enum FxSprKind {
    FxSprKind_Image       = 0, /**< single image / loose sprite set */
    FxSprKind_SpriteSheet = 1, /**< catalogue of independent sprites (GUI, fonts glyphs, ...) */
    FxSprKind_Animation   = 2, /**< sprite sheet + animation descriptor block (creatures) */
    FxSprKind_Font        = 3, /**< glyph catalogue */
};

#pragma pack(push, 1)

/** File header (24 bytes). Directory and payload are located via the explicit
 *  byte offsets below so future optional blocks can be inserted between the
 *  header and the directory without breaking readers. */
struct FxSprHeader {
    char     magic[4];       /**< "FXSP" */
    uint16_t version;        /**< == FXSPR_VERSION */
    uint16_t flags;          /**< bitwise OR of FxSprFlags */
    uint16_t kind;           /**< FxSprKind */
    uint16_t reserved;       /**< 0 */
    uint32_t entry_count;    /**< number of directory entries (includes sentinel 0) */
    uint32_t directory_off;  /**< byte offset from file start to the directory */
    uint32_t payload_off;    /**< byte offset from file start to the RGBA payload */
};

/** Directory entry (8 bytes). One per sprite, in `.tab` order. */
struct FxSprEntry {
    uint32_t data_off;       /**< byte offset into the payload of this sprite's RGBA8 */
    uint16_t width;          /**< pixels; == indexed .tab SWidth (parity contract) */
    uint16_t height;         /**< pixels; == indexed .tab SHeight (parity contract) */
};

#pragma pack(pop)

/** Payload is entry_count blocks of width*height*4 bytes RGBA8, top-to-bottom,
 *  straight (non-premultiplied) alpha unless FxSprFlag_PremultipliedAlpha set.
 *  A sprite's block begins at (payload_off + entry.data_off). */

enum {
    FXSPR_HEADER_SIZE = 24, /**< sizeof(struct FxSprHeader) with pack(1) */
    FXSPR_ENTRY_SIZE  = 8,  /**< sizeof(struct FxSprEntry)  with pack(1) */
    FXSPR_BYTES_PER_PIXEL = 4
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KEEPERFX_KFX_ASSETS_FXSPRFORMAT_H */
