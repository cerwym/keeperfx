#include "platform.h"
#include "platform/PlatformManager.h"
#include "platform/PlatformLinux.h"
#include "cdrom.h"
#include "kfx_memory.h"

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
