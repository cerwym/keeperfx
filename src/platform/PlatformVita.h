#ifndef PLATFORM_VITA_H
#define PLATFORM_VITA_H

#include "platform/IPlatform.h"
#include "platform/IAudioPlatform.h"

/** Vita-specific IAudioPlatform — wraps vita_fmv_audio_* C functions. */
class VitaAudioPlatform : public IAudioPlatform {
public:
    bool    FmvAudioOpen(int freq, int channels) override;
    void    FmvAudioQueue(const void* pcm, int bytes) override;
    void    FmvAudioClose() override;
    int64_t FmvAudioPtsNs() override;
};

/** PS Vita implementation of IPlatform using VitaSDK sceIo dirent API. */
class PlatformVita : public IPlatform {
public:
    const char* GetOSVersion() const override;
    const void* GetImageBase() const override;
    const char* GetWineVersion() const override;
    const char* GetWineHost() const override;

    void ErrorParachuteInstall() override;
    void ErrorParachuteUpdate() override;

    TbFileFind* FileFindFirst(const char* filespec, TbFileEntry* entry) override;
    int32_t     FileFindNext(TbFileFind* handle, TbFileEntry* entry) override;
    void        FileFindEnd(TbFileFind* handle) override;

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

    TbBool FileExists(const char* path) const override;
    int    MakeDirectory(const char* path) override;
    int    GetCurrentDirectory(char* buf, unsigned long buflen) override;

    void        SetArgv(int argc, char** argv) override;
    const char* GetDataPath() const override;
    const char* GetSavePath() const override;

    void   SetRedbookVolume(SoundVolume vol) override;
    TbBool PlayRedbookTrack(int track) override;
    void   PauseRedbookTrack() override;
    void   ResumeRedbookTrack() override;
    void   StopRedbookTrack() override;

    void LogWrite(const char* message) override;

    void SystemInit() override;
    void FrameTick() override;
    void WorkTick() override;
    size_t GetScratchSize() const override;
    size_t GetPolyPoolSize() const override;

    IAudioPlatform* GetAudio() override;

private:
    VitaAudioPlatform m_audio;
};

#endif // PLATFORM_VITA_H
