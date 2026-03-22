#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/PlatformLinux.h"
#include <SDL2/SDL.h>
#include <string>
#include <memory>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fnmatch.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "bflib_crash.h"
#include "post_inc.h"

// TbFileFind is defined here; it is an opaque type to all callers.
struct TbFileFind {
    std::string filespec;
    std::string path;
    std::string namebuf;
    DIR*        handle = nullptr;
    bool        is_pattern = false;
};

// TbFileInfo is defined here; it is an opaque type to all callers.
struct TbFileInfo { FILE* fp; };

// ----- OS information -----

const char* PlatformLinux::GetOSVersion() const
{
    return "Linux";
}

const void* PlatformLinux::GetImageBase() const
{
    return nullptr;
}

const char* PlatformLinux::GetWineVersion() const
{
    return nullptr;
}

const char* PlatformLinux::GetWineHost() const
{
    return nullptr;
}

// ----- Crash / error parachute -----

void PlatformLinux::ErrorParachuteInstall()
{
    signal(SIGHUP,  ctrl_handler);
    signal(SIGQUIT, ctrl_handler);
}

void PlatformLinux::ErrorParachuteUpdate()
{
}

void PlatformLinux::VideoInit()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    atexit(SDL_Quit);
}

// ----- File system helpers -----

TbBool PlatformLinux::FileExists(const char* path) const
{
    return access(path, F_OK) == 0;
}

int PlatformLinux::MakeDirectory(const char* path)
{
    if (mkdir(path, 0755) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

int PlatformLinux::GetCurrentDirectory(char* buf, unsigned long buflen)
{
    if (getcwd(buf, buflen) == NULL) return -1;
    int len = strlen(buf);
    if (len > 1 && buf[len - 2] == '\\') buf[len - 2] = '\0';
    return 1;
}

// ----- File enumeration -----

static bool filespec_is_pattern(const char* filespec)
{
    return strchr(filespec, '*') != nullptr;
}

static std::string directory_from_filespec(const char* filespec)
{
    const auto sep = strrchr(filespec, '/');
    if (sep && sep != filespec) {
        return std::string(filespec, sep - filespec);
    }
    return ".";
}

static bool find_file(TbFileFind* ff, TbFileEntry* fe)
{
    while (true) {
        auto de = readdir(ff->handle);
        if (!de) {
            return false;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        const std::string path = ff->path + "/" + de->d_name;
        ff->namebuf = de->d_name;
        fe->Filename = ff->namebuf.c_str();
        if (ff->is_pattern) {
            if (fnmatch(ff->filespec.c_str(), path.c_str(), FNM_FILE_NAME | FNM_CASEFOLD) != 0) {
                continue;
            }
        }
        struct stat sb;
        if (stat(path.c_str(), &sb) < 0) {
            continue;
        }
        if (!S_ISREG(sb.st_mode)) {
            continue;
        }
        return true;
    }
}

TbFileFind* PlatformLinux::FileFindFirst(const char* filespec, TbFileEntry* fe)
{
    try {
        auto ff = std::make_unique<TbFileFind>();
        ff->is_pattern = filespec_is_pattern(filespec);
        ff->filespec = filespec;
        if (ff->is_pattern) {
            ff->path   = directory_from_filespec(filespec);
            ff->handle = opendir(ff->path.c_str());
        } else {
            ff->path   = filespec;
            ff->handle = opendir(filespec);
        }
        if (ff->handle && find_file(ff.get(), fe)) {
            return ff.release();
        }
    } catch (...) {}
    return nullptr;
}

int32_t PlatformLinux::FileFindNext(TbFileFind* ff, TbFileEntry* fe)
{
    try {
        if (find_file(ff, fe)) {
            return 1;
        }
    } catch (...) {}
    return -1;
}

void PlatformLinux::FileFindEnd(TbFileFind* ff)
{
    if (ff) {
        closedir(ff->handle);
    }
    delete ff;
}

// ----- Path provider -----

void PlatformLinux::SetArgv(int argc, char** argv)
{
    if (argc < 1 || !argv || !argv[0] || argv[0][0] == '\0') {
        return;
    }
    snprintf(data_path_, sizeof(data_path_), "%s", argv[0]);
    char* end = strrchr(data_path_, '/');
    if (end) {
        *end = '\0';
    } else {
        snprintf(data_path_, sizeof(data_path_), ".");
    }
    snprintf(save_path_, sizeof(save_path_), "%s", data_path_);
}

const char* PlatformLinux::GetDataPath() const { return data_path_; }
const char* PlatformLinux::GetSavePath() const { return save_path_; }

// ----- CDROM / Redbook audio -----

void PlatformLinux::SetRedbookVolume(SoundVolume)
{
    // TODO: implement CDROM features
}

TbBool PlatformLinux::PlayRedbookTrack(int)
{
    // TODO: implement CDROM features
    return false;
}

void PlatformLinux::PauseRedbookTrack()
{
    // TODO: implement CDROM features
}

void PlatformLinux::ResumeRedbookTrack()
{
    // TODO: implement CDROM features
}

void PlatformLinux::StopRedbookTrack()
{
    // TODO: implement CDROM features
}

// ----- File I/O -----

TbFileHandle PlatformLinux::FileOpen(const char* fname, unsigned char accmode)
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

int PlatformLinux::FileClose(TbFileHandle handle)
{
    if (!handle) return -1;
    auto h = static_cast<TbFileInfo*>(handle);
    int r = fclose(h->fp);
    KfxFree(h);
    return r ? -1 : 0;
}

int PlatformLinux::FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (int)fread(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

long PlatformLinux::FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (long)fwrite(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

int PlatformLinux::FileSeek(TbFileHandle handle, long offset, unsigned char origin)
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

int PlatformLinux::FilePosition(TbFileHandle handle)
{
    if (!handle) return -1;
    return (int)ftell(static_cast<TbFileInfo*>(handle)->fp);
}

TbBool PlatformLinux::FileEof(TbFileHandle handle)
{
    if (!handle) return 1;
    return feof(static_cast<TbFileInfo*>(handle)->fp) ? 1 : 0;
}

short PlatformLinux::FileFlush(TbFileHandle handle)
{
    if (!handle) return 0;
    return fflush(static_cast<TbFileInfo*>(handle)->fp) == 0 ? 1 : 0;
}

long PlatformLinux::FileLength(const char* fname)
{
    FILE* fp = fopen(fname, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fclose(fp);
    return len;
}

int PlatformLinux::FileDelete(const char* fname)
{
    return remove(fname) ? -1 : 1;
}

IWindowSystem* PlatformLinux::GetWindowSystem()
{
    return GetSDLWindowSystem();
}
