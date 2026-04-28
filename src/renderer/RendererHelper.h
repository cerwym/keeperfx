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

#ifndef PLATFORM_3DS
/** Save an 8-bit paletted framebuffer to a PNG or BMP file.
 *  Palette colours are read from the global lbPalette array (6-bit DK format).
 *  File format is determined by the extension (.png or .bmp).
 *  @return true on success. */
bool RendererHelper_SaveIndexedImage(const uint8_t* pixels, int w, int h, int pitch,
                                     const char* path);
#endif

/******************************************************************************/
