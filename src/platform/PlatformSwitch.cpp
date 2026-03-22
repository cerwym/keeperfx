#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/PlatformSwitch.h"
#include "bflib_basics.h"
#include <stdio.h>
#include <stdlib.h>
#include "post_inc.h"

// TbFileFind opaque stub (Switch does not have file-find yet)
struct TbFileFind {};

// TbFileInfo is defined here; it is an opaque type to all callers.
struct TbFileInfo { FILE* fp; };

void PlatformSwitch::SystemInit()
{
    LbErrorLogSetup(GetDataPath(), "keeperfx.log", 5);
}

const char* PlatformSwitch::GetOSVersion() const    { return "Nintendo Switch"; }
const void* PlatformSwitch::GetImageBase() const    { return nullptr; }
const char* PlatformSwitch::GetWineVersion() const  { return nullptr; }
const char* PlatformSwitch::GetWineHost() const     { return nullptr; }

void PlatformSwitch::ErrorParachuteInstall()    {}
void PlatformSwitch::ErrorParachuteUpdate()     {}

TbFileFind* PlatformSwitch::FileFindFirst(const char*, TbFileEntry*) { return nullptr; }
int32_t     PlatformSwitch::FileFindNext(TbFileFind*, TbFileEntry*)  { return -1; }
void        PlatformSwitch::FileFindEnd(TbFileFind*)                  {}

TbBool PlatformSwitch::FileExists(const char*) const                      { return 0; }
int    PlatformSwitch::MakeDirectory(const char*)                         { return -1; }
int    PlatformSwitch::GetCurrentDirectory(char* buf, unsigned long len)  { if (buf && len) buf[0] = '\0'; return -1; }

void   PlatformSwitch::SetRedbookVolume(SoundVolume) {}
TbBool PlatformSwitch::PlayRedbookTrack(int)         { return false; }
void   PlatformSwitch::PauseRedbookTrack()           {}
void   PlatformSwitch::ResumeRedbookTrack()          {}
void   PlatformSwitch::StopRedbookTrack()            {}

void        PlatformSwitch::SetArgv(int, char**) {}
const char* PlatformSwitch::GetDataPath() const  { return "sdmc:/keeperfx"; }
const char* PlatformSwitch::GetSavePath() const  { return "sdmc:/keeperfx/save"; }

// ----- File I/O -----

TbFileHandle PlatformSwitch::FileOpen(const char* fname, unsigned char accmode)
{
    const char* mode;
    switch (accmode) {
        case Lb_FILE_MODE_NEW:       mode = "wb";  break;
        case Lb_FILE_MODE_OLD:       mode = "r+b"; break;
        case Lb_FILE_MODE_APPEND:    mode = "ab";  break;
        case Lb_FILE_MODE_READ_ONLY:
        default:                     mode = "rb";  break;
    }
    FILE* fp = fopen(fname, mode);
    if (!fp) return nullptr;
    auto h = static_cast<TbFileInfo*>(KfxAlloc(sizeof(TbFileInfo)));
    if (!h) { fclose(fp); return nullptr; }
    h->fp = fp;
    return h;
}

int PlatformSwitch::FileClose(TbFileHandle handle)
{
    if (!handle) return -1;
    auto h = static_cast<TbFileInfo*>(handle);
    int r = fclose(h->fp);
    KfxFree(h);
    return r ? -1 : 0;
}

int PlatformSwitch::FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (int)fread(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

long PlatformSwitch::FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (long)fwrite(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

int PlatformSwitch::FileSeek(TbFileHandle handle, long offset, unsigned char origin)
{
    if (!handle) return -1;
    int whence;
    switch (origin) {
        case Lb_FILE_SEEK_BEGINNING: whence = SEEK_SET; break;
        case Lb_FILE_SEEK_CURRENT:   whence = SEEK_CUR; break;
        case Lb_FILE_SEEK_END:       whence = SEEK_END; break;
        default:                     return -1;
    }
    return fseek(static_cast<TbFileInfo*>(handle)->fp, offset, whence);
}

int PlatformSwitch::FilePosition(TbFileHandle handle)
{
    if (!handle) return -1;
    return (int)ftell(static_cast<TbFileInfo*>(handle)->fp);
}

TbBool PlatformSwitch::FileEof(TbFileHandle handle)
{
    if (!handle) return 1;
    return feof(static_cast<TbFileInfo*>(handle)->fp) ? 1 : 0;
}

short PlatformSwitch::FileFlush(TbFileHandle handle)
{
    if (!handle) return 0;
    return fflush(static_cast<TbFileInfo*>(handle)->fp) == 0 ? 1 : 0;
}

long PlatformSwitch::FileLength(const char* fname)
{
    FILE* fp = fopen(fname, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fclose(fp);
    return len;
}

int PlatformSwitch::FileDelete(const char* fname)
{
    return remove(fname) ? -1 : 1;
}
