#include "pre_inc.h"
#include "platform/PlatformManager.h"
#include "platform/IWindowSystem.h"
#include "platform.h"
#include "bflib_fileio.h"
#include <tracy/Tracy.hpp>
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

// ----- Singleton storage -----

IPlatform* PlatformManager::s_instance = nullptr;

IPlatform* PlatformManager::Get()
{
    return s_instance;
}

void PlatformManager::Set(IPlatform* platform)
{
    delete s_instance;
    s_instance = platform;
    // Probably could do this in a nicer way, but fuck it for now.
    tracy::SetThreadName("KeeperFX Main Thread");

    // Configure Tracy plot presentation (step-graphs, subsystem colours).
    // Must run before any plot value is emitted.
    KFX_PLOT_CONFIG("WVR/VertCount",    tracy::PlotFormatType::Number, true,  true,  KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/DrawCmds",     tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/ShadowCmds",   tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/WorldTextCmds",      tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/KSprAtlasCacheSize", tracy::PlotFormatType::Number, true,  true,  KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/KSprAtlasHits",      tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_GPU);
    KFX_PLOT_CONFIG("WVR/KSprAtlasMisses",    tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_CPU);
    KFX_PLOT_CONFIG("UI/Quads",         tracy::PlotFormatType::Number, true,  true,  KFX_COLOR_RENDER_CPU);
    KFX_PLOT_CONFIG("UI/RemapQuads",    tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_CPU);
    KFX_PLOT_CONFIG("UI/Lines",         tracy::PlotFormatType::Number, true,  false, KFX_COLOR_RENDER_CPU);
    KFX_PLOT_CONFIG("Sim/CreatureCount",tracy::PlotFormatType::Number, true,  true,  KFX_COLOR_SIMULATION);
    KFX_PLOT_CONFIG("AI/ActivePlayers", tracy::PlotFormatType::Number, true,  false, KFX_COLOR_AI);
}

// ----- Default IWindowSystem (base class impl) -----
// Returned by IPlatform::GetWindowSystem() when the platform has not
// registered a more specific window system.  HasOSCursor() returns false
// so all grab/warp/focus logic in the engine is skipped on stub platforms.
static IWindowSystem s_defaultWindowSystem;

IWindowSystem* IPlatform::GetWindowSystem()
{
    return &s_defaultWindowSystem;
}

// ----- C-compatible wrappers -----
// Only functions previously duplicated in linux.cpp / windows.cpp are wrapped
// here.  Functions with dedicated cross-platform source files (bflib_crash.c,
// cdrom.cpp) are left to those files.

extern "C" const char* get_os_version()
{
    return PlatformManager::Get()->GetOSVersion();
}

extern "C" const void* get_image_base()
{
    return PlatformManager::Get()->GetImageBase();
}

extern "C" const char* get_wine_version()
{
    return PlatformManager::Get()->GetWineVersion();
}

extern "C" const char* get_wine_host()
{
    return PlatformManager::Get()->GetWineHost();
}

extern "C" void install_exception_handler()
{
    // Kept for backward compatibility — Vita's pre-SDL crash trap setup.
    // Platforms that need early crash trapping (before LbErrorParachuteInstall)
    // implement this via their own startup code.
}

extern "C" TbFileFind* LbFileFindFirst(const char* filespec, TbFileEntry* fe)
{
    return PlatformManager::Get()->FileFindFirst(filespec, fe);
}

extern "C" int32_t LbFileFindNext(TbFileFind* ff, TbFileEntry* fe)
{
    return PlatformManager::Get()->FileFindNext(ff, fe);
}

extern "C" void LbFileFindEnd(TbFileFind* ff)
{
    PlatformManager::Get()->FileFindEnd(ff);
}

extern "C" void PlatformManager_ErrorParachuteInstall()
{
    PlatformManager::Get()->ErrorParachuteInstall();
}

extern "C" void PlatformManager_ErrorParachuteUpdate()
{
    PlatformManager::Get()->ErrorParachuteUpdate();
}

extern "C" TbBool PlatformManager_FileExists(const char* path)
{
    return PlatformManager::Get()->FileExists(path);
}

extern "C" int PlatformManager_MakeDirectory(const char* path)
{
    return PlatformManager::Get()->MakeDirectory(path);
}

extern "C" int PlatformManager_GetCurrentDirectory(char* buf, unsigned long buflen)
{
    return PlatformManager::Get()->GetCurrentDirectory(buf, buflen);
}

extern "C" TbFileHandle PlatformManager_FileOpen(const char* fname, unsigned char accmode)
{
    return PlatformManager::Get()->FileOpen(fname, accmode);
}

extern "C" int PlatformManager_FileClose(TbFileHandle handle)
{
    return PlatformManager::Get()->FileClose(handle);
}

extern "C" int PlatformManager_FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    return PlatformManager::Get()->FileRead(handle, buf, len);
}

extern "C" long PlatformManager_FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    return PlatformManager::Get()->FileWrite(handle, buf, len);
}

extern "C" int PlatformManager_FileSeek(TbFileHandle handle, long offset, unsigned char origin)
{
    return PlatformManager::Get()->FileSeek(handle, offset, origin);
}

extern "C" int PlatformManager_FilePosition(TbFileHandle handle)
{
    return PlatformManager::Get()->FilePosition(handle);
}

extern "C" TbBool PlatformManager_FileEof(TbFileHandle handle)
{
    return PlatformManager::Get()->FileEof(handle);
}

extern "C" short PlatformManager_FileFlush(TbFileHandle handle)
{
    return PlatformManager::Get()->FileFlush(handle);
}

extern "C" long PlatformManager_FileLength(const char* fname)
{
    return PlatformManager::Get()->FileLength(fname);
}

extern "C" int PlatformManager_FileDelete(const char* fname)
{
    return PlatformManager::Get()->FileDelete(fname);
}

extern "C" void PlatformManager_LogWrite(const char* message)
{
    IPlatform* p = PlatformManager::Get();
    if (p) p->LogWrite(message);
}

extern "C" const char* PlatformManager_GetSavePath()
{
    return PlatformManager::Get()->GetSavePath();
}

extern "C" const char* PlatformManager_GetDataPath()
{
    return PlatformManager::Get()->GetDataPath();
}

extern "C" void PlatformManager_SetArgv(int argc, char** argv)
{
    PlatformManager::Get()->SetArgv(argc, argv);
}

extern "C" void PlatformManager_FrameTick()
{
    IPlatform* p = PlatformManager::Get();
    if (p) p->FrameTick();
}

extern "C" void PlatformManager_WorkTick()
{
    IPlatform* p = PlatformManager::Get();
    if (p) p->WorkTick();
}

extern "C" size_t PlatformManager_GetScratchSize()
{
    return PlatformManager::Get()->GetScratchSize();
}

extern "C" size_t PlatformManager_GetPolyPoolSize()
{
    return PlatformManager::Get()->GetPolyPoolSize();
}

extern "C" TbBool PlatformManager_ForcesAllModesAvailable()
{
    IPlatform* p = PlatformManager::Get();
    return p ? p->ForcesAllModesAvailable() : false;
}

extern "C" TbBool PlatformManager_OwnsDisplay()
{
    IPlatform* p = PlatformManager::Get();
    return p ? p->OwnsDisplay() : false;
}

IAudioPlatform* PlatformManager_GetAudio()
{
    IPlatform* p = PlatformManager::Get();
    return p ? p->GetAudio() : nullptr;
}
