// Native "open file" dialog implementation.
//
// Kept in its own TU so windows.h stays out of the ImGui / engine sources that
// include NativeFileDialog.hpp. The Win32 backend uses the classic
// GetOpenFileName (comdlg32); other platforms get a no-op stub for now.
#include "pre_inc.h"
#include "kfx/imgui/NativeFileDialog.hpp"

#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <commdlg.h>
#  pragma comment(lib, "comdlg32.lib")
#endif

#include "post_inc.h"

namespace kfx {

#if defined(_WIN32)

// Convert a "human|pattern|human|pattern" spec into the NUL/NUL-terminated
// double-string GetOpenFileName expects. Returns the number of bytes written.
static void build_win32_filter(const char* filter, char* buf, std::size_t bufSize)
{
    if (buf == nullptr || bufSize == 0)
        return;
    std::size_t w = 0;
    auto put = [&](const char* s) {
        while (*s && w + 1 < bufSize)
            buf[w++] = *s++;
        if (w + 1 < bufSize)
            buf[w++] = '\0';
    };
    if (filter && *filter)
    {
        const char* p = filter;
        while (*p)
        {
            const char* bar = std::strchr(p, '|');
            std::size_t len = bar ? (std::size_t)(bar - p) : std::strlen(p);
            for (std::size_t i = 0; i < len && w + 1 < bufSize; ++i)
                buf[w++] = p[i];
            if (w + 1 < bufSize)
                buf[w++] = '\0';
            p += len;
            if (bar)
                ++p;
        }
    }
    else
    {
        put("All files");
        put("*.*");
    }
    // Final extra terminator.
    if (w < bufSize)
        buf[w++] = '\0';
}

bool OpenFileDialog(const char* title, const char* initialDir, const char* filter,
                    char* outPath, std::size_t outSize)
{
    if (outPath == nullptr || outSize == 0)
        return false;
    outPath[0] = '\0';

    char filterBuf[512];
    build_win32_filter(filter, filterBuf, sizeof(filterBuf));

    OPENFILENAMEA ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filterBuf;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = outPath;
    ofn.nMaxFile = (DWORD)outSize;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (initialDir && *initialDir) ? initialDir : nullptr;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    return GetOpenFileNameA(&ofn) != FALSE;
}

#else // !_WIN32

bool OpenFileDialog(const char*, const char*, const char*, char* outPath, std::size_t outSize)
{
    if (outPath && outSize)
        outPath[0] = '\0';
    return false; // no native picker on this platform yet
}

#endif

} // namespace kfx
