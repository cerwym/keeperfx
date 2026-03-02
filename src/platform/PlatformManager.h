#ifndef PLATFORM_MANAGER_H
#define PLATFORM_MANAGER_H

#ifdef __cplusplus
#  include "IPlatform.h"

/** Singleton owner and accessor for the active IPlatform implementation.
 *
 *  Call PlatformManager::Set() once at program startup (before kfxmain) to
 *  register the platform implementation.  The C-compatible wrappers defined
 *  in PlatformManager.cpp then delegate to the registered instance.
 */
class PlatformManager {
public:
    static IPlatform* Get();
    static void       Set(IPlatform* platform);

private:
    static IPlatform* s_instance;
};
#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/** C-callable wrappers — safe to include and call from C translation units. */
const char* PlatformManager_GetDataPath(void);
const char* PlatformManager_GetSavePath(void);
void        PlatformManager_SetArgv(int argc, char** argv);
void        PlatformManager_ErrorParachuteInstall(void);
void        PlatformManager_ErrorParachuteUpdate(void);
TbBool      PlatformManager_FileExists(const char* path);
int         PlatformManager_MakeDirectory(const char* path);
int         PlatformManager_GetCurrentDirectory(char* buf, unsigned long buflen);
TbFileHandle PlatformManager_FileOpen(const char* fname, unsigned char accmode);
int          PlatformManager_FileClose(TbFileHandle handle);
int          PlatformManager_FileRead(TbFileHandle handle, void* buf, unsigned long len);
long         PlatformManager_FileWrite(TbFileHandle handle, const void* buf, unsigned long len);
int          PlatformManager_FileSeek(TbFileHandle handle, long offset, unsigned char origin);
int          PlatformManager_FilePosition(TbFileHandle handle);
TbBool       PlatformManager_FileEof(TbFileHandle handle);
short        PlatformManager_FileFlush(TbFileHandle handle);
long         PlatformManager_FileLength(const char* fname);
int          PlatformManager_FileDelete(const char* fname);
void        PlatformManager_LogWrite(const char* message);
/** Called once per frame to allow the platform to perform per-frame housekeeping
 *  (e.g. prevent screen blanking on Vita via sceKernelPowerTick). */
void        PlatformManager_FrameTick(void);
void        PlatformManager_WorkTick(void);
size_t      PlatformManager_GetScratchSize(void);
size_t      PlatformManager_GetPolyPoolSize(void);

#ifdef __cplusplus
} // extern "C"

/** Returns the platform audio sub-interface, or nullptr for headless/desktop CI.
 *  C++-only (IAudioPlatform is a C++ class).  Call from .cpp translation units. */
IAudioPlatform* PlatformManager_GetAudio(void);
#endif

#endif // PLATFORM_MANAGER_H
