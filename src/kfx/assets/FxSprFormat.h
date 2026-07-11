/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FxSprFormat.h
 *     On-disk layout of the `.fxspr` truecolour sprite container.
 *
 *     `.fxspr` is the canonical-RGBA sprite container described in
 *     docs/sprite-asset-unification.md (section 5). It replaces the split
 *     `.dat` + `.tab` (+ `.pal`) representation with one self-describing,
 *     little-endian artifact carrying straight RGBA8 pixels.
 *
 *     This header is the SINGLE SOURCE OF TRUTH for the byte layout: both the
 *     engine loader and the offline transcoder/packer must agree with it.
 *
 *     -- Versions -------------------------------------------------------------
 *     v1 (bootstrap): 24-byte header + 8-byte directory entries + raw RGBA8
 *        payload. `Image` / `SpriteSheet` kinds only. flags == FxSprFlag_Dims16.
 *     v2 (rich, self-describing): the 24-byte v1 header is followed by a 24-byte
 *        extension (FxSprHeaderExt2). Directory entries become 32-byte
 *        FxSprEntryRich carrying per-sprite metadata that mirrors struct
 *        KeeperSprite (creature_graphics.h): offsets, shadow, frame/rotation
 *        grouping, frame_flags, plus a stable group_id, a name (offset into a
 *        string table) and a category. A small FxSprAssetInfo block carries
 *        file-level variant facts (scale tier, colour mode, provenance, display
 *        name). The RGBA payload is zlib(deflate)-compressed
 *        (FxSprFlag_PayloadCompressed) and inflates to payload_raw_size bytes;
 *        entry.data_off indexes into the INFLATED payload. Readers dispatch on
 *        `version`; v1 files remain readable.
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

/** Current container version written by this build. Readers also accept v1. */
#define FXSPR_VERSION     2u
#define FXSPR_VERSION_V1  1u

/** Header flag bits. */
enum FxSprFlags {
    FxSprFlag_PremultipliedAlpha = 0x0001, /**< bit0: 0=straight, 1=premultiplied */
    FxSprFlag_Dims16             = 0x0002, /**< bit1: dims are 16-bit (always set) */
    FxSprFlag_PayloadCompressed  = 0x0004, /**< bit2: payload is a single zlib stream */
    FxSprFlag_IndexedCache       = 0x0008, /**< bit3: precomputed R8+RLE block present (unused) */
    FxSprFlag_RichEntries        = 0x0010, /**< bit4: directory uses 32-byte FxSprEntryRich (v2) */
};

/** Asset kind discriminator. One container, not sub-formats. */
enum FxSprKind {
    FxSprKind_Image       = 0, /**< single image / loose sprite set */
    FxSprKind_SpriteSheet = 1, /**< catalogue of independent sprites (GUI, fonts glyphs, ...) */
    FxSprKind_Animation   = 2, /**< sprite sheet + animation descriptor block (creatures) */
    FxSprKind_Font        = 3, /**< glyph catalogue */
};

/** Per-sprite semantic category (FxSprEntryRich.category). Answers the unified
 *  "what is this image for" question that the legacy anonymous-by-ID data lacks. */
enum FxSprCategory {
    FxSprCat_Unknown  = 0,
    FxSprCat_Gui      = 1,
    FxSprCat_Font     = 2,
    FxSprCat_Pointer  = 3,
    FxSprCat_Creature = 4,
    FxSprCat_Object   = 5,
    FxSprCat_Room     = 6,
    FxSprCat_Texture  = 7,
    FxSprCat_Effect   = 8,
};

/** Where a variant came from (FxSprAssetInfo.provenance). Lets the tools keep
 *  Bullfrog's original art cleanly separable from KeeperFX's and third-party mods. */
enum FxSprProvenance {
    FxSprProv_Unknown  = 0,
    FxSprProv_Bullfrog = 1,
    FxSprProv_KeeperFX = 2,
    FxSprProv_Mod      = 3,
};

/** Colour source of a variant (FxSprAssetInfo.colour_mode). */
enum FxSprColourMode {
    FxSprColour_IndexedDerived = 0, /**< materialised from indexed art through a palette */
    FxSprColour_Truecolour     = 1, /**< straight 24-bit source RGBA */
};

/** Camera view of a sprite frame (FxSprEntryRich.view). */
enum FxSprView {
    FxSprView_TopDown     = 0, /**< isometric / top-down (also used for flat GUI) */
    FxSprView_FirstPerson = 1, /**< first-person */
};

#pragma pack(push, 1)

/** File header prefix (24 bytes). Identical across v1 and v2 so the magic,
 *  version and block offsets can always be read the same way. Directory and
 *  payload are located via the explicit byte offsets below. */
struct FxSprHeader {
    char     magic[4];       /**< "FXSP" */
    uint16_t version;        /**< FXSPR_VERSION (2) or FXSPR_VERSION_V1 (1) */
    uint16_t flags;          /**< bitwise OR of FxSprFlags */
    uint16_t kind;           /**< FxSprKind */
    uint16_t reserved;       /**< 0 */
    uint32_t entry_count;    /**< number of directory entries (includes sentinel 0) */
    uint32_t directory_off;  /**< byte offset from file start to the directory */
    uint32_t payload_off;    /**< byte offset from file start to the RGBA payload */
};

/** v2 header extension (24 bytes), present immediately after FxSprHeader when
 *  version == 2. Locates the optional metadata blocks and describes payload
 *  compression + directory stride. */
struct FxSprHeaderExt2 {
    uint16_t entry_stride;      /**< bytes per directory entry (FXSPR_ENTRY_RICH_SIZE) */
    uint16_t assetinfo_size;    /**< bytes of the asset-info block (0 if none) */
    uint32_t assetinfo_off;     /**< byte offset to FxSprAssetInfo (0 if none) */
    uint32_t stringtable_off;   /**< byte offset to the string table (0 if none) */
    uint32_t stringtable_size;  /**< bytes of the string table */
    uint32_t payload_size;      /**< ON-DISK payload bytes at payload_off (compressed) */
    uint32_t payload_raw_size;  /**< INFLATED payload bytes (== sum of w*h*4) */
};

/** v1 directory entry (8 bytes). One per sprite, in `.tab` order. */
struct FxSprEntry {
    uint32_t data_off;       /**< byte offset into the payload of this sprite's RGBA8 */
    uint16_t width;          /**< pixels; == indexed .tab SWidth (parity contract) */
    uint16_t height;         /**< pixels; == indexed .tab SHeight (parity contract) */
};

/** v2 rich directory entry (32 bytes). Superset of FxSprEntry with per-sprite
 *  metadata mirroring struct KeeperSprite plus identity/name/category. */
struct FxSprEntryRich {
    uint32_t data_off;       /**< byte offset into the INFLATED payload */
    uint16_t width;          /**< pixels; == indexed .tab SWidth (parity contract) */
    uint16_t height;         /**< pixels; == indexed .tab SHeight (parity contract) */
    int16_t  offset_x;       /**< draw origin X (KeeperSprite.offset_x) */
    int16_t  offset_y;       /**< draw origin Y (KeeperSprite.offset_y) */
    int16_t  shadow_offset;  /**< KeeperSprite.shadow_offset */
    uint16_t frame_flags;    /**< KeeperSprite.frame_flags */
    uint32_t group_id;       /**< stable logical sprite / animation identity */
    uint16_t frame_index;    /**< position within an animation (0 for flat sheets) */
    uint8_t  rotation;       /**< LR direction (0 = flat / non-rotatable) */
    uint8_t  view;           /**< FxSprView */
    uint32_t name_off;       /**< byte offset into the string table (0 = unnamed) */
    uint16_t category;       /**< FxSprCategory */
    uint16_t reserved;       /**< 0 */
};

/** v2 file-level variant facts (12 bytes). A whole .fxspr file is ONE variant. */
struct FxSprAssetInfo {
    uint16_t scale;          /**< nominal scale tier (e.g. 32/64/128); 0 = unspecified */
    uint8_t  colour_mode;    /**< FxSprColourMode */
    uint8_t  provenance;     /**< FxSprProvenance */
    uint32_t name_off;       /**< display/collection name, offset into the string table */
    uint32_t reserved;       /**< 0 */
};

#pragma pack(pop)

/** Payload is entry_count blocks of width*height*4 bytes RGBA8, top-to-bottom,
 *  straight (non-premultiplied) alpha unless FxSprFlag_PremultipliedAlpha set.
 *  A sprite's block begins at (inflated payload + entry.data_off).
 *  When FxSprFlag_PayloadCompressed is set (v2 default), the on-disk region
 *  [payload_off, payload_off+payload_size) is a single zlib stream inflating to
 *  payload_raw_size bytes; otherwise it is that many raw bytes.
 *  The string table is a blob of NUL-terminated UTF-8 names; byte 0 is '\0' so a
 *  name_off of 0 denotes the empty (unnamed) string. */

enum {
    FXSPR_HEADER_SIZE      = 24, /**< sizeof(struct FxSprHeader) with pack(1) */
    FXSPR_HEADER_EXT2_SIZE = 24, /**< sizeof(struct FxSprHeaderExt2) with pack(1) */
    FXSPR_ENTRY_SIZE       = 8,  /**< sizeof(struct FxSprEntry) with pack(1) */
    FXSPR_ENTRY_RICH_SIZE  = 32, /**< sizeof(struct FxSprEntryRich) with pack(1) */
    FXSPR_ASSETINFO_SIZE   = 12, /**< sizeof(struct FxSprAssetInfo) with pack(1) */
    FXSPR_BYTES_PER_PIXEL  = 4
};

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KEEPERFX_KFX_ASSETS_FXSPRFORMAT_H */
