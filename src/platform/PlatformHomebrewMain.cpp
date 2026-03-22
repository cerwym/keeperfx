#include "pre_inc.h"
#include "platform.h"
#include "platform/PlatformManager.h"
#include "kfx_memory.h"
#include <SDL2/SDL_log.h>
#include <stdio.h>
#ifdef PLATFORM_VITA
#include "platform/PlatformVita.h"
#elif defined(PLATFORM_3DS)
#include "platform/Platform3DS.h"
#elif defined(PLATFORM_SWITCH)
#include "platform/PlatformSwitch.h"
#endif
#include "post_inc.h"

#if defined(PLATFORM_VITA) || defined(PLATFORM_3DS) || defined(PLATFORM_SWITCH)

static FILE* sdl_log_file = nullptr;

static void sdl_log_callback(void* /*userdata*/, int /*category*/, SDL_LogPriority /*priority*/, const char* message)
{
    if (sdl_log_file) {
        fprintf(sdl_log_file, "%s\n", message);
        fflush(sdl_log_file);
    }
}

int main(int argc, char* argv[]) {
#if defined(PLATFORM_VITA)
    // Create log file before SystemInit so the boot-log writes inside have a file to append to.
    { FILE* _f = fopen("ux0:data/keeperfx/kfx_boot.log", "w"); if (_f) { fprintf(_f, "main-enter\n"); fclose(_f); } }
#endif

#if defined(PLATFORM_VITA)
    PlatformManager::Set(new PlatformVita());
#elif defined(PLATFORM_3DS)
    PlatformManager::Set(new Platform3DS());
#elif defined(PLATFORM_SWITCH)
    PlatformManager::Set(new PlatformSwitch());
#endif

    // SystemInit: platform-specific early setup (shacccg load, clocks, cwd, early log).
    PlatformManager::Get()->SystemInit();
    KfxMemInit();

    // Route SDL log output to a file before SDL_Init is called in VideoInit.
    {
        char sdl_log_path[256];
        snprintf(sdl_log_path, sizeof(sdl_log_path), "%s/sdl.log", PlatformManager_GetDataPath());
        sdl_log_file = fopen(sdl_log_path, "w");
        SDL_LogSetOutputFunction(sdl_log_callback, nullptr);
    }

    PlatformManager::Get()->VideoInit();
    PlatformManager::Get()->InputInit();
    PlatformManager::Get()->AudioInit();

    return kfxmain(argc, argv);
}

#endif // PLATFORM_VITA || PLATFORM_3DS || PLATFORM_SWITCH
