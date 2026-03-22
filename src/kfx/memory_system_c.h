#ifndef KFX_MEMORY_SYSTEM_C_H
#define KFX_MEMORY_SYSTEM_C_H

#include <stddef.h>

/** @file memory_system_c.h
 *  @brief C bridge for the KeeperFX managed memory registry.
 *
 *  This API provides a minimal contract for C and C++ systems to register
 *  shared buffers, assert required capacity before writes, and query the
 *  currently tracked capacity by resource id.
 *
 *  Typical flow for legacy C callsites:
 *  @code{.c}
 *  static unsigned char* texture_page;
 *
 *  kfx_memory_register_external_buffer(
 *      "texture.page.main", (void**)&texture_page, 0,
 *      KFX_DOMAIN_GAMEPLAY_STATIC,
 *      KFX_MANAGED_MUTABLE | KFX_MANAGED_EXTERNAL);
 *
 *  if (!kfx_memory_ensure_capacity("texture.page.main", BLOCK_MEM_SIZE)) {
 *      return false;
 *  }
 *
 *  memset(texture_page, 130, BLOCK_MEM_SIZE);
 *  @endcode
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KfxManagedDomain {
    /** Resource exists for the full process lifetime. */
    KFX_DOMAIN_PROCESS = 0,
    /** Resource is needed only during startup/bootstrap. */
    KFX_DOMAIN_BOOT,
    /** Resource belongs to menu/frontend systems. */
    KFX_DOMAIN_FRONTEND,
    /** Gameplay resource that persists across level transitions. */
    KFX_DOMAIN_GAMEPLAY_STATIC,
    /** Resource is rebuilt or discarded per loaded level. */
    KFX_DOMAIN_LEVEL_TRANSIENT,
    /** Reusable cache-like storage that can be repopulated on demand. */
    KFX_DOMAIN_STREAMING_CACHE,
    /** Platform-specific resource lifetime scope. */
    KFX_DOMAIN_PLATFORM_LOCAL,
} KfxManagedDomain;

typedef enum KfxManagedFlags {
    /** Buffer contents may be written/changed after registration. */
    KFX_MANAGED_MUTABLE = 1 << 0,
    /** Buffer pointer is owned externally and tracked by the manager. */
    KFX_MANAGED_EXTERNAL = 1 << 1,
} KfxManagedFlags;

/** @brief Initializes the managed memory registry.
 *
 *  Safe to call more than once; repeated calls keep the registry initialized.
 */
void kfx_memory_system_init(void);

/** @brief Shuts down the managed memory registry.
 *
 *  This clears resource metadata tracked by the registry.
 */
void kfx_memory_system_shutdown(void);

/** @brief Registers a resource buffer pointer under a stable resource id.
 *  @param resource_id Unique stable id, for example texture.page.main.
 *  @param buffer_ptr Address of the tracked buffer pointer variable.
 *  @param initial_capacity Current known capacity in bytes.
 *  @param domain Lifetime grouping used for ownership semantics.
 *  @param flags Resource behavior flags.
 *  @return Non-zero on success, zero on invalid input or failure.
 */
int kfx_memory_register_external_buffer(
    const char* resource_id,
    void** buffer_ptr,
    size_t initial_capacity,
    KfxManagedDomain domain,
    unsigned int flags);

/** @brief Ensures that a registered resource can hold required_capacity bytes.
 *  @param resource_id Registered resource id.
 *  @param required_capacity Required byte capacity.
 *  @return Non-zero if capacity is guaranteed after call, zero on failure.
 */
int kfx_memory_ensure_capacity(const char* resource_id, size_t required_capacity);

/** @brief Returns tracked capacity for a resource.
 *  @param resource_id Registered resource id.
 *  @return Capacity in bytes, or 0 if not found/uninitialized.
 */
size_t kfx_memory_get_capacity(const char* resource_id);

/** @brief Frees all external buffers registered under the given domain.
 *
 *  Registry entries are kept so callers do not need to re-register.
 *  Tracked capacity is reset to zero so the next kfx_memory_ensure_capacity
 *  triggers a fresh allocation.
 *
 *  Typical usage at level reset:
 *  @code{.c}
 *  kfx_memory_release_domain(KFX_DOMAIN_LEVEL_TRANSIENT);
 *  @endcode
 *
 *  @param domain The lifetime domain to release.
 *  @return Number of resources released.
 */
int kfx_memory_release_domain(KfxManagedDomain domain);

#ifdef __cplusplus
}
#endif

#endif