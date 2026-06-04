/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file bflib_render_types.h
 *     Shared polygon vertex and block type definitions.
 * @par Comment:
 *     Extracted from bflib_render.h so that non-SW renderer code can use
 *     PolyPoint without pulling in SW rasteriser function declarations.
 *     Include bflib_render.h (not this file) in SW renderer source.
 */
/******************************************************************************/
#ifndef BFLIB_RENDER_TYPES_H
#define BFLIB_RENDER_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct PolyPoint {
    long X; /* Horizontal coordinate within screen buffer */
    long Y; /* Vertical coordinate within screen buffer */
    long U; /* Texture UV mapping, U coordinate */
    long V; /* Texture UV mapping, V coordinate */
    long S; /* Shininess / brightness of the point */
};

struct GtBlock { /* sizeof = 48 */
    unsigned char *texturedata;
    unsigned long width;
    unsigned long height;
    unsigned long lightness0;
    unsigned long lightness1;
    unsigned long lightness3;
    unsigned long lightness2;
    unsigned long texturestride;
    unsigned long scalingfactor;
    unsigned long colorformat;
    unsigned long renderflags;
    unsigned long textureoffset;
};

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* BFLIB_RENDER_TYPES_H */
