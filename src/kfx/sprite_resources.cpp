#include "kfx/sprite_resources.h"

#include <cstdio>

#include "kfx_memory.h"
#include "kfx/memory_system_c.h"
#include "globals.h"
#include "bflib_fileio.h"
#include "config.h"
#include "engine_render.h"
#include "front_simple.h"

namespace {

bool g_spriteHeapRegistryReady = false;
char g_spriteHeapResourceIds[KEEPSPRITE_LENGTH][32];

void registerSpriteHeapResources()
{
    if (g_spriteHeapRegistryReady) {
        return;
    }

    for (long i = 0; i < KEEPSPRITE_LENGTH; i++) {
        std::snprintf(
            g_spriteHeapResourceIds[i], sizeof(g_spriteHeapResourceIds[i]),
            "sprite.frame.%ld", i);
        kfx_memory_register_external_buffer(
            g_spriteHeapResourceIds[i],
            reinterpret_cast<void**>(&sprite_heap_handle[i]),
            0,
            KFX_DOMAIN_LEVEL_TRANSIENT,
            KFX_MANAGED_MUTABLE | KFX_MANAGED_EXTERNAL);
    }
    g_spriteHeapRegistryReady = true;
}

} // namespace

extern "C" int setup_heap_manager(void)
{
    const char* fname;
    long i;

#ifdef SPRITE_FORMAT_V2
    fname = get_game_file_path_fmt(FGrp_StdData, "thingspr-%d.jty", 32);
#else
    fname = prepare_file_path(FGrp_StdData, "creature.jty");
#endif
    JUSTLOG("setup_heap_manager: opening \"%s\"", fname != nullptr ? fname : "");
    jty_file_handle = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (!jty_file_handle) {
        ERRORLOG("Can not open JTY file, \"%s\"", fname);
        return 0;
    }

    registerSpriteHeapResources();
    // keepsprite[] are derived lookup pointers not tracked by the registry.
    for (i = 0; i < KEEPSPRITE_LENGTH; i++) {
        keepsprite[i] = nullptr;
        sprite_heap_handle[i] = nullptr;
    }
    return 1;
}

extern "C" void reset_heap_manager(void)
{
    if (jty_file_handle) {
        LbFileClose(jty_file_handle);
    }
    jty_file_handle = nullptr;

    // Release all level-transient memory in one pass through the registry.
    // Any future subsystems registering KFX_DOMAIN_LEVEL_TRANSIENT are
    // automatically freed here without touching this function.
    kfx_memory_release_domain(KFX_DOMAIN_LEVEL_TRANSIENT);

    // keepsprite[] are raw derived pointers not owned by the registry;
    // they must be cleared manually after the domain release.
    for (long i = 0; i < KEEPSPRITE_LENGTH; i++) {
        keepsprite[i] = nullptr;
    }
}

extern "C" int he_ensure_sprite_frame(unsigned short kspr_idx, size_t size)
{
    if (kspr_idx >= KEEPSPRITE_LENGTH || size == 0) {
        return 0;
    }

    registerSpriteHeapResources();
    return kfx_memory_ensure_capacity(g_spriteHeapResourceIds[kspr_idx], size);
}

extern "C" void* he_alloc(size_t size)
{
    return KfxAlloc(size);
}
