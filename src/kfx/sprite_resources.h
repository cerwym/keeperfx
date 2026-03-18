#ifndef KFX_SPRITE_RESOURCES_H
#define KFX_SPRITE_RESOURCES_H

#include <stddef.h>

/** @file sprite_resources.h
 *  @brief Sprite/JTY asset lifecycle managed through the KeeperFX memory registry.
 *
 *  This module owns the JTY file handle and registers each keeper-sprite frame
 *  buffer as a KFX_DOMAIN_LEVEL_TRANSIENT resource so that level resets
 *  automatically free all frames via kfx_memory_release_domain().
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Open the JTY file and register all sprite frame buffers with the
 *         memory registry. Must be called once per level setup.
 *  @return Non-zero on success, zero if the JTY file cannot be opened.
 */
int setup_heap_manager(void);

/** @brief Close the JTY file and release all KFX_DOMAIN_LEVEL_TRANSIENT
 *         resources (sprite frames and any other transient registrants).
 */
void reset_heap_manager(void);

/** @brief Ensure the memory registry has allocated capacity for a single
 *         keeper-sprite frame before a read into it.
 *  @param kspr_idx Frame index into sprite_heap_handle[].
 *  @param size     Required byte capacity.
 *  @return Non-zero on success, zero on failure.
 */
int he_ensure_sprite_frame(unsigned short kspr_idx, size_t size);

/** @brief Thin allocation helper; routes through KfxAlloc.
 *  @param size Allocation size in bytes.
 *  @return Allocated pointer or NULL on failure.
 */
void* he_alloc(size_t size);

#ifdef __cplusplus
}
#endif

#endif /* KFX_SPRITE_RESOURCES_H */
