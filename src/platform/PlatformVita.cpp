#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/PlatformVita.h"
#ifdef PLATFORM_VITA
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>
#include <psp2/kernel/threadmgr.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "bflib_crash.h"
#include "bflib_basics.h"
#include "bflib_filelst.h"
#include "config.h"
#include "audio/audio_vita.h"
#endif
#include "post_inc.h"

#ifdef PLATFORM_VITA

/* Link-time heap and stack declarations required by vitasdk.
 * These are read by the linker/loader before main() runs.
 *
 * Memory budget (256 MB process limit on HENkaku):
 *   Code (.text):    ~4 MB
 *   Data + BSS:    ~114 MB  (struct Game=51 MB, block_mem=34 MB, poly_pool=16 MB, ...)
 *   Main stack:      4 MB
 *   Heap (below):  128 MB
 *   Total:        ~250 MB  (leaves ~6 MB headroom for kernel/system allocations)
 *
 * 128 MB heap is needed for vitaGL's GXM ring buffers (~32 MB vertex pool
 * default + overhead) plus SDL, audio, and runtime objects.  BSS is ~114 MB,
 * so 192 MB heap pushes total to ~314 MB and causes newlib abort() before
 * main() — do not raise above 128 MB. */
int _newlib_heap_size_user  = 128 * 1024 * 1024; // 128 MB heap — 250 MB total (text+BSS+stack+heap), within 256 MB
int sceUserMainThreadStackSize = 4 * 1024 * 1024; // 4 MB main thread stack

// TbFileFind is defined here; it is an opaque type to all callers.
struct TbFileFind {
    SceUID      handle;
    char        namebuf[256];
    char        pattern[256]; // empty means no filtering
};

// TbFileInfo is defined here; it is an opaque type to all callers.
struct TbFileInfo {
    SceUID fd;
};

// Forward declaration — defined in the SystemInit section below.
extern "C" const char* vita_modify_load_filename(const char* input);

// ----- OS information -----

const char* PlatformVita::GetOSVersion() const
{
    return "PS Vita";
}

const void* PlatformVita::GetImageBase() const
{
    return nullptr;
}

const char* PlatformVita::GetWineVersion() const
{
    return nullptr;
}

const char* PlatformVita::GetWineHost() const
{
    return nullptr;
}

// ----- Crash / error parachute -----

static void vita_crash_handler(int sig)
{
    FILE* f = fopen("ux0:data/keeperfx/crash.log", "a");
    if (f) {
        fprintf(f, "KeeperFX crashed: signal %d\n", sig);
        fclose(f);
    }
    sceClibPrintf("KeeperFX CRASH: signal %d\n", sig);
    sceKernelExitProcess(1);
}

void PlatformVita::ErrorParachuteInstall()
{
    signal(SIGHUP,  ctrl_handler);
    signal(SIGQUIT, ctrl_handler);
    // Override crash signals with the Vita handler that writes crash.log + exits
    signal(SIGSEGV, vita_crash_handler);
    signal(SIGABRT, vita_crash_handler);
    signal(SIGFPE,  vita_crash_handler);
    signal(SIGILL,  vita_crash_handler);
}

void PlatformVita::ErrorParachuteUpdate()
{
}

// ----- File system helpers -----

TbBool PlatformVita::FileExists(const char* path) const
{
    SceIoStat stat;
    return sceIoGetstat(vita_modify_load_filename(path), &stat) >= 0;
}

int PlatformVita::MakeDirectory(const char* path)
{
    int ret = sceIoMkdir(vita_modify_load_filename(path), 0777);
    if (ret >= 0 || ret == (int)0x80010011 /* SCE_ERROR_ERRNO_EEXIST */) return 0;
    return -1;
}

int PlatformVita::GetCurrentDirectory(char* buf, unsigned long buflen)
{
    // On Vita CWD is app0: (read-only); return the data path instead
    snprintf(buf, buflen, "%s", GetDataPath());
    return 1;
}

// ----- File enumeration helpers -----

static bool vita_name_matches_pattern(const char* name, const char* pattern)
{
    if (pattern[0] == '\0') {
        return true;
    }
    // Simple '*' wildcard matching (case-insensitive not attempted on Vita)
    const char* p = pattern;
    const char* n = name;
    while (*p && *n) {
        if (*p == '*') {
            p++;
            if (*p == '\0') {
                return true; // trailing '*' matches anything
            }
            while (*n) {
                if (vita_name_matches_pattern(n, p)) {
                    return true;
                }
                n++;
            }
            return false;
        }
        if (*p != *n) {
            return false;
        }
        p++;
        n++;
    }
    return *p == '\0' && *n == '\0';
}

static bool vita_find_next_entry(TbFileFind* ff, TbFileEntry* fe)
{
    SceIoDirent de;
    while (sceIoDread(ff->handle, &de) > 0) {
        if (!SCE_S_ISREG(de.d_stat.st_mode)) {
            continue;
        }
        if (!vita_name_matches_pattern(de.d_name, ff->pattern)) {
            continue;
        }
        strncpy(ff->namebuf, de.d_name, sizeof(ff->namebuf) - 1);
        ff->namebuf[sizeof(ff->namebuf) - 1] = '\0';
        fe->Filename = ff->namebuf;
        return true;
    }
    return false;
}

// ----- File enumeration -----

TbFileFind* PlatformVita::FileFindFirst(const char* filespec, TbFileEntry* fe)
{
    // Resolve relative path before splitting into dir/pattern
    filespec = vita_modify_load_filename(filespec);
    // Determine directory and optional pattern from filespec
    const char* slash   = strrchr(filespec, '/');
    const char* pattern = slash ? slash + 1 : filespec;
    char        dir[256];
    if (slash && slash != filespec) {
        size_t len = (size_t)(slash - filespec);
        if (len >= sizeof(dir)) {
            return nullptr;
        }
        strncpy(dir, filespec, len);
        dir[len] = '\0';
    } else {
        strncpy(dir, ".", sizeof(dir));
    }

    SceUID dfd = sceIoDopen(dir);
    if (dfd < 0) {
        return nullptr;
    }

    auto ff = static_cast<TbFileFind*>(KfxAlloc(sizeof(TbFileFind)));
    if (!ff) {
        sceIoDclose(dfd);
        return nullptr;
    }
    ff->handle = dfd;
    strncpy(ff->pattern, strchr(filespec, '*') ? pattern : "", sizeof(ff->pattern) - 1);
    ff->pattern[sizeof(ff->pattern) - 1] = '\0';
    ff->namebuf[0] = '\0';

    if (vita_find_next_entry(ff, fe)) {
        return ff;
    }
    sceIoDclose(ff->handle);
    KfxFree(ff);
    return nullptr;
}

int32_t PlatformVita::FileFindNext(TbFileFind* ff, TbFileEntry* fe)
{
    if (!ff) {
        return -1;
    }
    return vita_find_next_entry(ff, fe) ? 1 : -1;
}

void PlatformVita::FileFindEnd(TbFileFind* ff)
{
    if (ff) {
        sceIoDclose(ff->handle);
        KfxFree(ff);
    }
}

// ----- File I/O -----

TbFileHandle PlatformVita::FileOpen(const char* fname, unsigned char accmode)
{
    int flags;
    switch (accmode) {
        case Lb_FILE_MODE_NEW:       flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC;  break;
        case Lb_FILE_MODE_OLD:       flags = SCE_O_RDWR;                                 break;
        case Lb_FILE_MODE_APPEND:    flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND; break;
        case Lb_FILE_MODE_READ_ONLY:
        default:                     flags = SCE_O_RDONLY;                               break;
    }
    SceUID fd = sceIoOpen(vita_modify_load_filename(fname), flags, 0777);
    if (fd < 0) {
        WARNLOG("sceIoOpen(\"%s\") failed: 0x%08X", fname ? fname : "(null)", (unsigned)fd);
        return nullptr;
    }
    auto h = static_cast<TbFileInfo*>(KfxAlloc(sizeof(TbFileInfo)));
    if (!h) { sceIoClose(fd); return nullptr; }
    h->fd = fd;
    return h;
}

int PlatformVita::FileClose(TbFileHandle handle)
{
    if (!handle) return -1;
    auto h = static_cast<TbFileInfo*>(handle);
    int r = sceIoClose(h->fd);
    KfxFree(h);
    return (r < 0) ? -1 : 0;
}

int PlatformVita::FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    if (!handle) return -1;
    return sceIoRead(static_cast<TbFileInfo*>(handle)->fd, buf, len);
}

long PlatformVita::FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    if (!handle) return -1;
    return sceIoWrite(static_cast<TbFileInfo*>(handle)->fd, buf, len);
}

int PlatformVita::FileSeek(TbFileHandle handle, long offset, unsigned char origin)
{
    if (!handle) return -1;
    int whence;
    switch (origin) {
        case Lb_FILE_SEEK_BEGINNING: whence = SCE_SEEK_SET; break;
        case Lb_FILE_SEEK_CURRENT:   whence = SCE_SEEK_CUR; break;
        case Lb_FILE_SEEK_END:       whence = SCE_SEEK_END; break;
        default:                     return -1;
    }
    return (int)sceIoLseek(static_cast<TbFileInfo*>(handle)->fd, offset, whence);
}

int PlatformVita::FilePosition(TbFileHandle handle)
{
    if (!handle) return -1;
    return (int)sceIoLseek(static_cast<TbFileInfo*>(handle)->fd, 0, SCE_SEEK_CUR);
}

TbBool PlatformVita::FileEof(TbFileHandle handle)
{
    if (!handle) return 1;
    SceUID fd = static_cast<TbFileInfo*>(handle)->fd;
    SceOff cur = sceIoLseek(fd, 0, SCE_SEEK_CUR);
    SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, cur, SCE_SEEK_SET);
    return (cur >= end) ? 1 : 0;
}

short PlatformVita::FileFlush(TbFileHandle handle)
{
    // sceIo has no mid-write flush — writes are committed immediately
    (void)handle;
    return 1;
}

long PlatformVita::FileLength(const char* fname)
{
    SceUID fd = sceIoOpen(vita_modify_load_filename(fname), SCE_O_RDONLY, 0);
    if (fd < 0) return -1;
    long len = (long)sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);
    return len;
}

int PlatformVita::FileDelete(const char* fname)
{
    return (sceIoRemove(vita_modify_load_filename(fname)) < 0) ? -1 : 1;
}

// ----- CDROM / Redbook audio (no-ops on Vita) -----

void PlatformVita::SetRedbookVolume(SoundVolume) {}
TbBool PlatformVita::PlayRedbookTrack(int) { return false; }
void PlatformVita::PauseRedbookTrack() {}
void PlatformVita::ResumeRedbookTrack() {}
void PlatformVita::StopRedbookTrack() {}

void PlatformVita::LogWrite(const char* message)
{
    sceClibPrintf("KeeperFX: %s", message);
}

// ----- Hardware / OS initialisation -----

// Resolve relative FName paths to absolute ux0: paths.
// TbLoadFilesV2 arrays use literal relative paths (e.g. "data/creature.tab").
// VitaSDK's POSIX chdir does not affect all fopen sites, so we prepend
// keeper_runtime_directory here instead.
extern "C" const char *vita_modify_load_filename(const char *input)
{
    // Special markers: pass through unchanged.
    if (input[0] == '*' || input[0] == '!') return input;
    // Already absolute (Vita 'ux0:' or POSIX '/').
    if (strchr(input, ':') || input[0] == '/') return input;
    // Relative path: prepend keeper_runtime_directory.
    static char resolved[2048];
    snprintf(resolved, sizeof(resolved), "%s/%s", keeper_runtime_directory, input);
    return resolved;
}

void PlatformVita::SystemInit()
{
#define _SYSI_LOG(msg) do { FILE* _f = fopen("ux0:data/keeperfx/kfx_boot.log", "a"); if (_f) { fprintf(_f, "sysinit:" msg "\n"); fclose(_f); } } while(0)
    _SYSI_LOG("enter");
    // Maximise CPU/GPU clocks — default is throttled; same settings as vitaQuakeII.
    scePowerSetArmClockFrequency(444);
    _SYSI_LOG("arm-clk");
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    _SYSI_LOG("clks-done");
    // Disable VFP/FPU exception traps on the main thread; without this, any
    // denormal or other edge-case float operation in the game code will trap.
    sceKernelChangeThreadVfpException(0x0800009FU, 0x0);
    _SYSI_LOG("vfp");
    // chdir so that any POSIX-relative opens outside LbDataLoad also resolve.
    chdir(GetDataPath());
    _SYSI_LOG("chdir");
    // Override the data-load filename modifier so that relative paths stored in
    // TbLoadFilesV2.FName (e.g. "data/creature.tab") are resolved to absolute
    // ux0: paths. VitaSDK's POSIX chdir does not reliably affect all fopen call
    // sites, so we prefix keeper_runtime_directory explicitly instead.
    LbDataLoadSetModifyFilenameFunction(vita_modify_load_filename);
    _SYSI_LOG("modify-fn");
    // Allocate the permanent gameplay scratch buffer.  Must happen before any
    // gameplay code runs.  Sized at 256 KB — enough for the largest algorithm
    // use (BFS flood-fill queue, ~116 KB) with headroom.  Sprite PNG decode and
    // ZIP/JSON config parsing use their own transient local allocations.
    extern unsigned char *big_scratch;
    big_scratch = (unsigned char *)KfxAlloc(256 * 1024);
    _SYSI_LOG("malloc-done");
#undef _SYSI_LOG
}

void PlatformVita::FrameTick()
{
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); // prevent screen blanking
}

void PlatformVita::WorkTick()
{
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT); // prevent auto-suspend during loading
}

// ----- Path provider -----

void        PlatformVita::SetArgv(int, char**) {} // argv[0] unused on Vita
const char* PlatformVita::GetDataPath() const { return "ux0:data/keeperfx"; }
const char* PlatformVita::GetSavePath() const { return "ux0:data/keeperfx/save"; }
size_t      PlatformVita::GetScratchSize() const { return 2 * 1024 * 1024; } /* 2 MB -- conservative until BSS is reduced */
size_t      PlatformVita::GetPolyPoolSize() const { return 4 * 1024 * 1024; } /* 4 MB -- reduced from 16 MB desktop default */

// ----- Audio sub-interface -----

bool VitaAudioPlatform::FmvAudioOpen(int freq, int channels)
{
    return vita_fmv_audio_open(freq, channels) != 0;
}

void VitaAudioPlatform::FmvAudioQueue(const void* pcm, int bytes)
{
    vita_fmv_audio_queue(pcm, bytes);
}

void VitaAudioPlatform::FmvAudioClose()
{
    vita_fmv_audio_close();
}

int64_t VitaAudioPlatform::FmvAudioPtsNs()
{
    return vita_fmv_audio_pts_ns();
}

IAudioPlatform* PlatformVita::GetAudio()
{
    return &m_audio;
}

#endif // PLATFORM_VITA
