#include "platform.h"
#include "platform/PlatformManager.h"
#include "platform/PlatformLinux.h"
#include "cdrom.h"
#include "kfx_memory.h"

// Steam is not supported on Linux; the symbols are required by the linker.
extern "C" int steam_api_init()   { return 0; }
extern "C" void steam_api_shutdown() {}

// CDROM is Windows-only; provide no-op stubs for Linux.
extern "C" void   SetRedbookVolume(SoundVolume) {}
extern "C" TbBool PlayRedbookTrack(int)         { return false; }
extern "C" void   PauseRedbookTrack()           {}
extern "C" void   ResumeRedbookTrack()          {}
extern "C" void   StopRedbookTrack()            {}

extern "C" int main(int argc, char *argv[]) {
    PlatformManager::Set(new PlatformLinux());
    PlatformManager::Get()->VideoInit();
    KfxMemInit();
    return kfxmain(argc, argv);
}
