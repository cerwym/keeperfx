#ifndef PLATFORM_SWITCH_H
#define PLATFORM_SWITCH_H

#include "platform/IPlatform.h"

/** Nintendo Switch implementation of IPlatform (stub — extend as needed). */
class PlatformSwitch : public IPlatform {
public:
    void SystemInit() override;
    const char* GetOSVersion() const override;
    const void* GetImageBase() const override;
    const char* GetWineVersion() const override;
    const char* GetWineHost() const override;

    void ErrorParachuteInstall() override;
    void ErrorParachuteUpdate() override;

    TbFileFind* FileFindFirst(const char* filespec, TbFileEntry* entry) override;
    int32_t     FileFindNext(TbFileFind* handle, TbFileEntry* entry) override;
    void        FileFindEnd(TbFileFind* handle) override;

    TbBool FileExists(const char* path) const override;
    int    MakeDirectory(const char* path) override;
    int    GetCurrentDirectory(char* buf, unsigned long buflen) override;

    TbFileHandle FileOpen(const char* fname, unsigned char accmode) override;
    int          FileClose(TbFileHandle handle) override;
    int          FileRead(TbFileHandle handle, void* buf, unsigned long len) override;
    long         FileWrite(TbFileHandle handle, const void* buf, unsigned long len) override;
    int          FileSeek(TbFileHandle handle, long offset, unsigned char origin) override;
    int          FilePosition(TbFileHandle handle) override;
    TbBool       FileEof(TbFileHandle handle) override;
    short        FileFlush(TbFileHandle handle) override;
    long         FileLength(const char* fname) override;
    int          FileDelete(const char* fname) override;

    void        SetArgv(int argc, char** argv) override;
    const char* GetDataPath() const override;
    const char* GetSavePath() const override;

    void   SetRedbookVolume(SoundVolume vol) override;
    TbBool PlayRedbookTrack(int track) override;
    void   PauseRedbookTrack() override;
    void   ResumeRedbookTrack() override;
    void   StopRedbookTrack() override;
};

#endif // PLATFORM_SWITCH_H
