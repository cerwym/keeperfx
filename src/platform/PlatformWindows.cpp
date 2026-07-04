#include "kfx_memory.h"
#include "pre_inc.h"
#include "platform/PlatformWindows.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>
#include <excpt.h>
// imagehlp.h omitted: dbghelp.h supersedes it and defines the same types.
// Including both causes C2011 type redefinition errors on MSVC.
#include <dbghelp.h>
#include <psapi.h>
#include <direct.h>
#include <io.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <malloc.h>
#include <errno.h>
#include "bflib_crash.h"
#include "bflib_video.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

// TbFileFind is defined here; it is an opaque type to all callers.
struct TbFileFind {
    HANDLE handle;
    char*  namebuf;
    int    namebuflen;
};

// TbFileInfo is defined here; it is an opaque type to all callers.
struct TbFileInfo { FILE* fp; };

void DebugPrint(const std::string& msg) {
#ifdef UNICODE
    // Convert UTF-8/ANSI string to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, nullptr, 0);
    if (len > 0) {
        std::wstring wmsg(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, msg.c_str(), -1, &wmsg[0], len);
        OutputDebugStringW(wmsg.c_str());
    }
#else
    OutputDebugStringA(msg.c_str());
#endif
}


// ----- OS information -----

const char* PlatformWindows::GetOSVersion() const
{
    static char buffer[256];
    OSVERSIONINFO v;
    v.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (GetVersionEx(&v)) {
        snprintf(buffer, sizeof(buffer), "%s %ld.%ld.%ld",
            (v.dwPlatformId == VER_PLATFORM_WIN32_NT) ? "Windows NT" : "Windows",
            v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
        return buffer;
    }
    return "unknown";
}

const void* PlatformWindows::GetImageBase() const
{
    return GetModuleHandle(NULL);
}

const char* PlatformWindows::GetWineVersion() const
{
    const auto module = GetModuleHandle("ntdll.dll");
    if (module) {
        const auto wine_get_version =
            (const char* (WINAPI*)())(void*)GetProcAddress(module, "wine_get_version");
        if (wine_get_version) {
            return wine_get_version();
        }
    }
    return nullptr;
}

const char* PlatformWindows::GetWineHost() const
{
    const auto module = GetModuleHandle("ntdll.dll");
    static char buffer[256];
    if (module) {
        const auto wine_get_host_version =
            (void (WINAPI*)(const char**, const char**))(void*)
            GetProcAddress(module, "wine_get_host_version");
        if (wine_get_host_version) {
            const char* sys_name     = nullptr;
            const char* release_name = nullptr;
            wine_get_host_version(&sys_name, &release_name);
            snprintf(buffer, sizeof(buffer), "%s %s",
                sys_name     ? sys_name     : "unknown",
                release_name ? release_name : "unknown");
            return buffer;
        }
    }
    return nullptr;
}

// ----- Crash / error parachute -----

/** MinGW fallback: resolve one address against a GNU ld "keeperfx.map" file.
 *  MSVC builds carry a PDB and never reach this path (SymFromAddr succeeds).
 *  Returns true when a matching map entry was found and logged. */
static bool
_map_file_lookup(FILE *mapFile, int64_t keeperFxBaseAddr, int frame_no,
                 const char *module_name, DWORD64 pc, DWORD64 module_base)
{
    if (!mapFile || strncmp(module_name, "keeperfx", strlen("keeperfx")) != 0)
        return false;

    char mapFileLine[512];
    int64_t checkAddr = (int64_t)(pc - module_base) + keeperFxBaseAddr;
    int64_t prevAddr = 0;
    char prevName[512];
    prevName[0] = 0;

    fseek(mapFile, 0, SEEK_SET);
    while (fgets(mapFileLine, sizeof(mapFileLine), mapFile) != NULL)
    {
        int64_t addr;
        char name[512];
        name[0] = 0;
        if (sscanf(mapFileLine, "%llx %[^\t\n]", &addr, name) == 2 ||
            sscanf(mapFileLine, " .text %llx %[^\t\n]", &addr, name) == 2)
        {
            if (checkAddr > prevAddr && checkAddr < addr)
            {
                int64_t displacement = checkAddr - prevAddr;
                char *splitPos = strchr(prevName, ' ');
                if (strncmp(prevName, "0x", 2) == 0 && splitPos != NULL)
                {
                    memmove(prevName, splitPos + 1, strlen(splitPos));
                    char *lastSlash = strrchr(prevName, '/');
                    if (lastSlash) memmove(prevName, lastSlash + 1, strlen(lastSlash + 1) + 1);
                    memmove(prevName + 3, prevName, strlen(prevName) + 1);
                    memcpy(prevName, "-> ", 3);
                }
                LbJustLog("[#%-2d] %s!%s+0x%llx [0x%016llx]\t(map lookup)\n",
                          frame_no, module_name, prevName,
                          (unsigned long long)displacement, (unsigned long long)pc);
                return true;
            }
            prevAddr = addr;
            strcpy(prevName, name);
        }
    }
    return false;
}

static void
_backtrace(int depth, LPCONTEXT context)
{
    // MinGW map file (absent on MSVC builds, which resolve via PDB below).
    int64_t keeperFxBaseAddr = 0;
    FILE *mapFile = fopen("keeperfx.map", "r");
    if (mapFile)
    {
        char mapFileLine[512];
        while (fgets(mapFileLine, sizeof(mapFileLine), mapFile) != NULL)
        {
            if (sscanf(mapFileLine, " %*x __image_base__ = %llx", &keeperFxBaseAddr) == 1)
                break;
        }
        if (keeperFxBaseAddr == 0)
        {
            fclose(mapFile);
            mapFile = NULL;
        }
    }

    // StackWalk64 mutates the context it walks; use a copy so the caller's
    // exception context stays intact (the minidump snapshots the original).
    CONTEXT walk_ctx;
    memcpy(&walk_ctx, context, sizeof(CONTEXT));

    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));

#if defined(_M_X64)
    const DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = walk_ctx.Rip;
    frame.AddrStack.Offset = walk_ctx.Rsp;
    frame.AddrFrame.Offset = walk_ctx.Rbp;
#elif defined(_M_IX86)
    const DWORD machine_type = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset    = walk_ctx.Eip;
    frame.AddrStack.Offset = walk_ctx.Esp;
    frame.AddrFrame.Offset = walk_ctx.Ebp;
#else
    return;
#endif
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrStack.Mode   = AddrModeFlat;
    frame.AddrFrame.Mode   = AddrModeFlat;

    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    for (int frame_no = 0; frame_no < depth; frame_no++)
    {
        if (!StackWalk64(machine_type, process, thread, &frame, &walk_ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0)
            break;

        DWORD64 module_base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        const char *module_name = "[unknown module]";
        char module_name_raw[MAX_PATH];
        if (module_base && GetModuleFileNameA((HMODULE)(uintptr_t)module_base, module_name_raw, MAX_PATH))
        {
            module_name = strrchr(module_name_raw, '\\');
            if (module_name) module_name++;
            else             module_name = module_name_raw;
        }

        // Primary path: debug symbols (PDB). SYMOPT_LOAD_LINES was set by the
        // caller, so file:line resolution works for our own modules.
        char symbol_info[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)symbol_info;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen   = MAX_SYM_NAME;
        DWORD64 sym_disp = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &sym_disp, pSymbol))
        {
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(line);
            DWORD line_disp = 0;
            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &line_disp, &line))
            {
                LbJustLog("[#%-2d] %s!%s+0x%llx [%s:%lu]\n",
                          frame_no, module_name, pSymbol->Name,
                          (unsigned long long)sym_disp,
                          line.FileName, (unsigned long)line.LineNumber);
            }
            else
            {
                LbJustLog("[#%-2d] %s!%s+0x%llx [0x%016llx]\n",
                          frame_no, module_name, pSymbol->Name,
                          (unsigned long long)sym_disp,
                          (unsigned long long)frame.AddrPC.Offset);
            }
            continue;
        }

        // Fallback: MinGW map-file scan for keeperfx frames without symbols.
        if (_map_file_lookup(mapFile, keeperFxBaseAddr, frame_no,
                             module_name, frame.AddrPC.Offset, module_base))
            continue;

        LbJustLog("[#%-2d] %s : at 0x%016llx (base 0x%016llx)\n",
                  frame_no, module_name,
                  (unsigned long long)frame.AddrPC.Offset,
                  (unsigned long long)module_base);
    }
    if (mapFile) fclose(mapFile);
}

/** Write a minidump next to the exe so the crash can be opened post-mortem in
 *  Visual Studio or WinDbg together with keeperfx.pdb. Includes all thread
 *  stacks and memory referenced by them — much smaller than a full dump but
 *  enough to inspect locals and heap objects on every frame. */
static void _write_minidump(LPEXCEPTION_POINTERS info)
{
    char path[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(path, sizeof(path), "keeperfx_crash_%04u%02u%02u_%02u%02u%02u.dmp",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        LbErrorLog("Minidump: cannot create %s (error %lu)\n", path, GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = info;
    mei.ClientPointers = FALSE;
    const MINIDUMP_TYPE type = (MINIDUMP_TYPE)(MiniDumpScanMemory
        | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                file, type, &mei, nullptr, nullptr);
    CloseHandle(file);
    if (ok) LbErrorLog("Minidump written: %s\n", path);
    else    LbErrorLog("MiniDumpWriteDump failed (error %lu)\n", GetLastError());
}

static LONG CALLBACK ctrl_handler_w32(LPEXCEPTION_POINTERS info)
{
    switch (info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        switch (info->ExceptionRecord->ExceptionInformation[0]) {
        case 0: LbErrorLog("Attempt to read from inaccessible memory address %p.\n",
                           (void*)info->ExceptionRecord->ExceptionInformation[1]); break;
        case 1: LbErrorLog("Attempt to write to inaccessible memory address %p.\n",
                           (void*)info->ExceptionRecord->ExceptionInformation[1]); break;
        case 8: LbErrorLog("User-mode data execution prevention (DEP) violation at %p.\n",
                           (void*)info->ExceptionRecord->ExceptionInformation[1]); break;
        default: LbErrorLog("Memory access violation, code %d.\n", (int)info->ExceptionRecord->ExceptionInformation[0]); break;
        }
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        LbErrorLog("Attempt of integer division by zero.\n");
        break;
    default:
        LbErrorLog("Failure code %lx received.\n", info->ExceptionRecord->ExceptionCode);
        break;
    }
    LbErrorLog("Faulting instruction at %p on thread %lu.\n",
               info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());

    // Dump first: it snapshots the pristine exception context and every other
    // thread's stack, so even if symbolization below fails the crash is still
    // fully diagnosable offline against keeperfx.pdb.
    _write_minidump(info);

    // SYMOPT_LOAD_LINES makes SymGetLineFromAddr64 work (file:line frames);
    // SYMOPT_UNDNAME demangles C++ names; SYMOPT_FAIL_CRITICAL_ERRORS stops
    // dbghelp from raising interactive error boxes inside a crash handler.
    SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
                  | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
    // dbghelp allows exactly one symbol session per process handle. In
    // TRACY_ENABLE builds Tracy has already called SymInitialize(), so our
    // call fails with ERROR_INVALID_PARAMETER — that is NOT a real failure:
    // the session exists, we just don't own it. Reuse it (refreshing the
    // module list to pick up anything loaded since) and skip SymCleanup so we
    // don't tear down a session another subsystem is still using.
    HANDLE process = GetCurrentProcess();
    BOOL we_own_symbols = SymInitialize(process, NULL, TRUE);
    if (!we_own_symbols)
        SymRefreshModuleList(process);
    _backtrace(32, info->ContextRecord);
    if (we_own_symbols)
        SymCleanup(process);
    RendererResetScreen(true);
    LbErrorLogClose();
    return EXCEPTION_EXECUTE_HANDLER;
}

void PlatformWindows::ErrorParachuteInstall()
{
    signal(SIGBREAK, ctrl_handler);
    SetUnhandledExceptionFilter(ctrl_handler_w32);
}

void PlatformWindows::ErrorParachuteUpdate()
{
    SetUnhandledExceptionFilter(ctrl_handler_w32);
}

void PlatformWindows::VideoInit()
{
    // Allow the screensaver: SDL2 by default disables it via
    // SystemParametersInfo(SPI_SETSCREENSAVEACTIVE, FALSE), which tells Windows
    // the app wants exclusive display handling and can disrupt the HDR compositor.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK))
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    atexit(SDL_Quit);

    // Set GL pixel-format attributes BEFORE the first SDL_CreateWindow call.
    // On Windows, SDL2 calls SetPixelFormat inside SDL_CreateWindow itself
    // (WIN_GL_SetupWindow), so these values must be established here, not in
    // platform_create_gl_context which runs after the window already exists.
    // DOUBLEBUFFER and DEPTH_SIZE are critical: failure means single-buffered or
    // depthless rendering, both of which cause severe visual artefacts.
    //
    // We request 8-bit per channel (standard sRGB pixel format). On Windows,
    // a 10-bit (RGB10_A2) pixel format causes DWM to treat the framebuffer as
    // scRGB (linear light), but all palette data is sRGB-gamma-encoded, so
    // writing sRGB values into a linear framebuffer makes the image appear far
    // too bright on SDR monitors. The 8-bit sRGB pipeline correctly matches the
    // palette encoding. HDR support (10-bit + proper tone mapping) can be
    // revisited once a dedicated HDR rendering path exists.
    struct { int attr; int value; const char* name; } gl_attrs[] = {
        { SDL_GL_RED_SIZE,      8, "SDL_GL_RED_SIZE"     },
        { SDL_GL_GREEN_SIZE,    8, "SDL_GL_GREEN_SIZE"   },
        { SDL_GL_BLUE_SIZE,     8, "SDL_GL_BLUE_SIZE"    },
        { SDL_GL_ALPHA_SIZE,    8, "SDL_GL_ALPHA_SIZE"   },
        { SDL_GL_DOUBLEBUFFER,  1, "SDL_GL_DOUBLEBUFFER" },
        { SDL_GL_DEPTH_SIZE,   24, "SDL_GL_DEPTH_SIZE"   },
    };
    for (int i = 0; i < (int)(sizeof(gl_attrs)/sizeof(gl_attrs[0])); ++i)
    {
        if (!SDL_GL_SetAttribute((SDL_GLAttr)gl_attrs[i].attr, gl_attrs[i].value))
            fprintf(stderr, "PlatformWindows::VideoInit: %s=%d failed: %s\n",
                    gl_attrs[i].name, gl_attrs[i].value, SDL_GetError());
    }
}

// ----- File system helpers -----

TbBool PlatformWindows::FileExists(const char* path) const
{
    return _access(path, 0) == 0;
}

int PlatformWindows::MakeDirectory(const char* path)
{
    if (_mkdir(path) == 0) return 0;
    return (errno == EEXIST) ? 0 : -1;
}

// GetCurrentDirectory is a Windows API macro (→ GetCurrentDirectoryA/W); undef before method def.
#undef GetCurrentDirectory
int PlatformWindows::GetCurrentDirectory(char* buf, unsigned long buflen)
{
    if (_getcwd(buf, (int)buflen) == NULL) return -1;
    if (buf[1] == ':') memmove(buf, buf + 2, strlen(buf) - 1);
    int len = strlen(buf);
    if (len > 1 && buf[len - 2] == '\\') buf[len - 2] = '\0';
    return 1;
}

// ----- File enumeration -----

TbFileFind* PlatformWindows::FileFindFirst(const char* filespec, TbFileEntry* fentry)
{
    auto ffind = static_cast<TbFileFind*>(KfxAlloc(sizeof(TbFileFind)));
    if (!ffind) {
        return nullptr;
    }
    WIN32_FIND_DATA fd;
    ffind->handle = FindFirstFile(filespec, &fd);
    if (ffind->handle == INVALID_HANDLE_VALUE) {
        KfxFree(ffind);
        return nullptr;
    }
    const int namelen = strlen(fd.cFileName);
    ffind->namebuf = static_cast<char*>(KfxAlloc(namelen + 1));
    if (!ffind->namebuf) {
        FindClose(ffind->handle);
        KfxFree(ffind);
        return nullptr;
    }
    memcpy(ffind->namebuf, fd.cFileName, namelen + 1);
    ffind->namebuflen   = namelen;
    fentry->Filename    = ffind->namebuf;
    return ffind;
}

int32_t PlatformWindows::FileFindNext(TbFileFind* ffind, TbFileEntry* fentry)
{
    if (!ffind) {
        return -1;
    }
    WIN32_FIND_DATA fd;
    if (!FindNextFile(ffind->handle, &fd)) {
        return -1;
    }
    const int namelen = strlen(fd.cFileName);
    if (namelen > ffind->namebuflen) {
        auto buf = static_cast<char*>(KfxRealloc(ffind->namebuf, namelen + 1));
        if (!buf) {
            return -1;
        }
        ffind->namebuf    = buf;
        ffind->namebuflen = namelen;
    }
    memcpy(ffind->namebuf, fd.cFileName, namelen + 1);
    fentry->Filename = ffind->namebuf;
    return 1;
}

void PlatformWindows::FileFindEnd(TbFileFind* ffind)
{
    if (ffind) {
        FindClose(ffind->handle);
        KfxFree(ffind->namebuf);
        KfxFree(ffind);
    }
}

// ----- Path provider -----

void PlatformWindows::SetArgv(int argc, char** argv)
{
    if (argc < 1 || !argv || !argv[0] || argv[0][0] == '\0') {
        return;
    }
    snprintf(data_path_, sizeof(data_path_), "%s", argv[0]);
    // Strip filename component, keeping the directory.
    char* end = strrchr(data_path_, '\\');
    if (!end) end = strrchr(data_path_, '/');
    if (end) {
        *end = '\0';
    } else {
        snprintf(data_path_, sizeof(data_path_), ".");
    }
    snprintf(save_path_, sizeof(save_path_), "%s", data_path_);
}

const char* PlatformWindows::GetDataPath() const { return data_path_; }
const char* PlatformWindows::GetSavePath() const { return save_path_; }

const char* PlatformWindows::GetUserPrefDir()
{
    if (pref_path_[0] != '\0')
        return pref_path_;
    char* sdl_path = SDL_GetPrefPath("keeperfx", "keeperfx");
    if (sdl_path)
    {
        snprintf(pref_path_, sizeof(pref_path_), "%s", sdl_path);
        // SDL appends a trailing separator — strip it for consistency.
        size_t len = strlen(pref_path_);
        if (len > 0 && (pref_path_[len - 1] == '\\' || pref_path_[len - 1] == '/'))
            pref_path_[len - 1] = '\0';
        SDL_free(sdl_path);
    }
    else
    {
        snprintf(pref_path_, sizeof(pref_path_), "%s", data_path_);
    }
    return pref_path_;
}

// ----- CDROM / Redbook audio -----

void PlatformWindows::SetRedbookVolume(SoundVolume)
{
    // TODO: implement CDROM features
}

TbBool PlatformWindows::PlayRedbookTrack(int)
{
    // TODO: implement CDROM features
    return false;
}

void PlatformWindows::PauseRedbookTrack()
{
    // TODO: implement CDROM features
}

void PlatformWindows::ResumeRedbookTrack()
{
    // TODO: implement CDROM features
}

void PlatformWindows::StopRedbookTrack()
{
    // TODO: implement CDROM features
}

// ----- File I/O -----

TbFileHandle PlatformWindows::FileOpen(const char* fname, unsigned char accmode)
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

int PlatformWindows::FileClose(TbFileHandle handle)
{
    if (!handle) return -1;
    auto h = static_cast<TbFileInfo*>(handle);
    int r = fclose(h->fp);
    KfxFree(h);
    return r ? -1 : 0;
}

int PlatformWindows::FileRead(TbFileHandle handle, void* buf, unsigned long len)
{
    if (!handle) return -1;
    return (int)fread(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

long PlatformWindows::FileWrite(TbFileHandle handle, const void* buf, unsigned long len)
{
    if (!handle) {
        return -1;
    }

    DebugPrint(std::string(static_cast<const char*>(buf), len));

    return (long)fwrite(buf, 1, len, static_cast<TbFileInfo*>(handle)->fp);
}

int PlatformWindows::FileSeek(TbFileHandle handle, long offset, unsigned char origin)
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

int PlatformWindows::FilePosition(TbFileHandle handle)
{
    if (!handle) return -1;
    return (int)ftell(static_cast<TbFileInfo*>(handle)->fp);
}

TbBool PlatformWindows::FileEof(TbFileHandle handle)
{
    if (!handle) return 1;
    return feof(static_cast<TbFileInfo*>(handle)->fp) ? 1 : 0;
}

short PlatformWindows::FileFlush(TbFileHandle handle)
{
    if (!handle) return 0;
    return fflush(static_cast<TbFileInfo*>(handle)->fp) == 0 ? 1 : 0;
}

long PlatformWindows::FileLength(const char* fname)
{
    FILE* fp = fopen(fname, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fclose(fp);
    return len;
}

int PlatformWindows::FileDelete(const char* fname)
{
    return remove(fname) ? -1 : 1;
}

IWindowSystem* PlatformWindows::GetWindowSystem()
{
    return GetSDLWindowSystem();
}
