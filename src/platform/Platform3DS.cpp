#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/Platform3DS.h"
#include "bflib_basics.h"
#include <stdio.h>
#include <stdlib.h>
#include "post_inc.h"

// TbFileFind opaque stub (3DS does not have file-find yet)
struct TbFileFind {};

// TbFileInfo is defined here; it is an opaque type to all callers.
struct TbFileInfo { FILE* fp; };

void Platform3DS::SystemInit()
{
    LbErrorLogSetup(GetDataPath(), "keeperfx.log", 5);
}

const char* Platform3DS::GetOSVersion() const    { return "Nintendo 3DS"; }
const void* Platform3DS::GetImageBase() const    { return nullptr; }
const char* Platform3DS::GetWineVersion() const  { return nullptr; }
const char* Platform3DS::GetWineHost() const     { return nullptr; }

void Platform3DS::ErrorParachuteInstall()    {}
void Platform3DS::ErrorParachuteUpdate()     {}

TbFileFind* Platform3DS::FileFindFirst(const char*, TbFileEntry*) { return nullptr; }
int32_t     Platform3DS::FileFindNext(TbFileFind*, TbFileEntry*)  { return -1; }
void        Platform3DS::FileFindEnd(TbFileFind*)                  {}

TbBool Platform3DS::FileExists(const char*) const                      { return 0; }
int    Platform3DS::MakeDirectory(const char*)                         { return -1; }
int    Platform3DS::GetCurrentDirectory(char* buf, unsigned long len)  { if (buf && len) buf[0] = '\0'; return -1; }

void   Platform3DS::SetRedbookVolume(SoundVolume) {}
TbBool Platform3DS::PlayRedbookTrack(int)         { return false; }
void   Platform3DS::PauseRedbookTrack()           {}
void   Platform3DS::ResumeRedbookTrack()          {}
void   Platform3DS::StopRedbookTrack()            {}

void        Platform3DS::SetArgv(int, char**) {}
const char* Platform3DS::GetDataPath() const  { return "sdmc:/keeperfx"; }
const char* Platform3DS::GetSavePath() const  { return "sdmc:/keeperfx/save"; }

// ----- File I/O -----

TbFileHandle Platform3DS::FileOpen(const char* fname, unsigned char accmode)
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

int Platform3DS::FileClose(TbFileHandle handle)
{
    if (!handle) return -1;
    auto h = static_cast<TbFileInfo*>(handle);
    int r = fclose(h->fp);
    KfxFree(h);
    return r ? -1 : 0;
}

int Platform3DS::FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (int)fread(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

long Platform3DS::FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (long)fwrite(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

int Platform3DS::FileSeek(TbFileHandle handle, long offset, unsigned char origin)
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

int Platform3DS::FilePosition(TbFileHandle handle)
{
    if (!handle) return -1;
    return (int)ftell(static_cast<TbFileInfo*>(handle)->fp);
}

TbBool Platform3DS::FileEof(TbFileHandle handle)
{
    if (!handle) return 1;
    return feof(static_cast<TbFileInfo*>(handle)->fp) ? 1 : 0;
}

short Platform3DS::FileFlush(TbFileHandle handle)
{
    if (!handle) return 0;
    return fflush(static_cast<TbFileInfo*>(handle)->fp) == 0 ? 1 : 0;
}

long Platform3DS::FileLength(const char* fname)
{
    FILE* fp = fopen(fname, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fclose(fp);
    return len;
}

int Platform3DS::FileDelete(const char* fname)
{
    return remove(fname) ? -1 : 1;
}
