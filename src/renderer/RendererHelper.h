/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file RendererHelper.h
 *     Shared renderer utility functions.
 */
/******************************************************************************/
#pragma once

#include <cstdint>

/******************************************************************************/

/** Save an 8-bit paletted framebuffer to a PNG or BMP file.
 *  Palette colours are read from the global lbPalette array (6-bit DK format).
 *  File format is determined by the extension (.png or .bmp).
 *  @return true on success. */
bool RendererHelper_SaveIndexedImage(const uint8_t* pixels, int w, int h, int pitch,
                                     const char* path);

/** Save a 32-bit RGBA framebuffer to a PNG or BMP file.
 *  @param fmt  Screenshot format code: 2 = BMP, anything else = PNG.
 *  @return true on success. */
bool RendererHelper_SaveRGBAImage(const uint8_t* pixels, int w, int h, int pitch,
                                  int fmt, const char* path);

/******************************************************************************/
