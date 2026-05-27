/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GpuTypes.h
 *     Backend-agnostic GPU resource handle types.
 * @par Purpose:
 *     Provides a single opaque handle type for GPU textures that is wide enough
 *     to represent native handles from any supported graphics API:
 *       - OpenGL:  GLuint (32-bit) — zero-extends into the lower 32 bits.
 *       - Vulkan:  VkImageView / VkImage (64-bit non-dispatchable handle).
 *       - D3D11:   ID3D11ShaderResourceView* (64-bit pointer).
 *       - D3D12:   D3D12_GPU_DESCRIPTOR_HANDLE (64-bit integer).
 *
 *     No API-specific headers are included here; this file is safe to include
 *     from any translation unit including pure-C modules.
 */
/******************************************************************************/
#pragma once

#include <stdint.h>

/** Opaque 64-bit GPU texture / resource handle.
 *  Each backend casts its native handle type to/from this value.
 *  The value 0 is reserved as "invalid / not set". */
typedef uint64_t GpuTextureHandle;

/** Sentinel value indicating an uninitialised or absent GPU texture. */
#define kInvalidGpuTexture ((GpuTextureHandle)0)

/******************************************************************************/
