/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vita_malloc_wrap.c
 *     vitaGL unified-heap malloc wrappers for PlayStation Vita.
 * @par Purpose:
 *     Routes all stdlib heap calls through vitaGL's allocator when vitaGL is
 *     active.  This unifies the C stdlib heap and the vitaGL GPU memory pool
 *     into a single allocator, preventing cross-pool fragmentation — a large
 *     texture upload can use memory a game object just freed, and vice versa.
 * @par Comment:
 *     Activated via GCC --wrap linker flags added by CMakeLists when
 *     VITA_ENABLE_VITAGL is ON and vitaGL is found.  The __wrap_* convention
 *     means the linker silently replaces every call to malloc/free/etc. in the
 *     entire binary (game code + all linked libraries) with these functions,
 *     with zero source changes needed elsewhere.
 *     Technique from d3es-vita neo/sys/linux/main.cpp.
 * @author   KeeperFX Team
 * @date     03 Mar 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifdef VITA_HAVE_VITAGL

#include <stdint.h>
#include <string.h>
#include <vitaGL.h>

void *__wrap_malloc(uint32_t size)                          { return vglMalloc(size); }
void  __wrap_free(void *addr)                               { vglFree(addr); }
void *__wrap_calloc(uint32_t nmemb, uint32_t size)
{
    /* Do NOT call vglCalloc() here: vglCalloc() internally calls calloc(),
     * which --wrap,calloc redirects back to __wrap_calloc → infinite recursion
     * and a stack-overflow crash.  Replicate calloc semantics manually using
     * vglMalloc (a direct allocator primitive that does not call calloc). */
    size_t total = (size_t)nmemb * size;
    void *ptr = vglMalloc(total);
    if (ptr != NULL)
        memset(ptr, 0, total);
    return ptr;
}
void *__wrap_realloc(void *ptr, uint32_t size)              { return vglRealloc(ptr, size); }
void *__wrap_memalign(uint32_t alignment, uint32_t size)    { return vglMemalign(alignment, size); }

#endif /* VITA_HAVE_VITAGL */
