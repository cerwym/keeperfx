/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file platform_gl_sdl2.cpp
 *     OpenGL context management for SDL2-based desktop platforms.
 * @par Purpose:
 *     Implements platform_create_gl_context / platform_destroy_gl_context /
 *     platform_swap_gl_buffers for any platform using SDL2 as the windowing
 *     layer (Windows, Linux, macOS).
 * @par Windows note:
 *     SDL2 calls SetPixelFormat on the window's DC during SDL_CreateWindow
 *     (WIN_GL_SetupWindow) and loads wglCreateContextAttribsARB at the same
 *     time.  SDL_GL_CreateContext then uses that extension to create the 3.3
 *     Core context.  The GL pixel-format attributes (8-bit RGB, standard sRGB)
 *     are set in VideoInit before any window is created so SDL2 picks them up.
 *     The window is created with SDL_WINDOW_HIDDEN so that SetPixelFormat
 *     does not trigger a DWM composition-pipeline reconfiguration on HDR
 *     displays (which would cause a visible black flash).  The window is
 *     shown after the GL context is fully active.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform.h"
#include "platform/WindowSystemSDL.h"

#include <SDL2/SDL.h>
#ifdef _WIN32
#  include <SDL2/SDL_syswm.h>
#  include <dxgi1_6.h>
#endif
#ifdef __linux__
#  include <dlfcn.h>
#endif
#include "post_inc.h"
#include "bflib_basics.h"

/******************************************************************************/

#ifdef _WIN32
// ---------------------------------------------------------------------------
// DXGI / pixel-format diagnostics (Windows only)
// ---------------------------------------------------------------------------

static const char *dxgi_colorspace_name(DXGI_COLOR_SPACE_TYPE cs)
{
    switch (cs) {
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709:      return "sRGB (P709 G22 full)";
    case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:      return "linear RGB (P709 G10 full)";
    case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P709:    return "P709 G22 studio";
    case DXGI_COLOR_SPACE_RGB_STUDIO_G22_NONE_P2020:   return "P2020 G22 studio";
    case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020:   return "HDR10 P2020 PQ full [HDR ACTIVE]";
    case DXGI_COLOR_SPACE_RGB_STUDIO_G2084_NONE_P2020: return "P2020 PQ studio";
    case DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020:     return "WCG SDR (P2020 G22 full)";
    default:                                            return "unknown";
    }
}

static void log_dxgi_hdr_info(HWND hwnd)
{
    LbJustLog("[GL-diag] --- DXGI display info ---\n");
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)))
    {
        LbJustLog("[GL-diag]   CreateDXGIFactory1 failed\n");
        return;
    }
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    UINT ai = 0;
    IDXGIAdapter *adapter = nullptr;
    while (factory->EnumAdapters(ai, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        UINT oi = 0;
        IDXGIOutput *output = nullptr;
        while (adapter->EnumOutputs(oi, &output) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_OUTPUT_DESC odesc;
            output->GetDesc(&odesc);
            const bool isTarget = (odesc.Monitor == hMon);
            char devname[32] = {};
            WideCharToMultiByte(CP_UTF8, 0, odesc.DeviceName, -1, devname, sizeof(devname), nullptr, nullptr);
            LbJustLog("[GL-diag]   adapter[%u] output[%u] %-14s%s\n",
                ai, oi, devname, isTarget ? "  <<< game window" : "");
            IDXGIOutput6 *out6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), (void **)&out6)))
            {
                DXGI_OUTPUT_DESC1 d1;
                if (SUCCEEDED(out6->GetDesc1(&d1)))
                {
                    LbJustLog("[GL-diag]     ColorSpace    : %d (%s)\n",
                        (int)d1.ColorSpace, dxgi_colorspace_name(d1.ColorSpace));
                    LbJustLog("[GL-diag]     BitsPerColor  : %u\n", d1.BitsPerColor);
                    LbJustLog("[GL-diag]     Luminance     : max=%.1f  min=%.4f  maxFF=%.1f  (nits)\n",
                        d1.MaxLuminance, d1.MinLuminance, d1.MaxFullFrameLuminance);
                }
                out6->Release();
            }
            else
            {
                LbJustLog("[GL-diag]     (IDXGIOutput6 unavailable)\n");
            }
            output->Release();
            ++oi;
        }
        adapter->Release();
        ++ai;
    }
    factory->Release();
}

// Log DXGI HDR state and the pixel format SDL2 selected for the window.
static void log_window_diag(SDL_Window *window)
{
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi))
        return;

    HWND hwnd = wmi.info.win.window;
    log_dxgi_hdr_info(hwnd);

    HDC hdc = GetDC(hwnd);
    if (hdc)
    {
        int pfIdx = GetPixelFormat(hdc);
        PIXELFORMATDESCRIPTOR pfd = {};
        DescribePixelFormat(hdc, pfIdx, sizeof(pfd), &pfd);
        LbJustLog("[GL-diag] SDL2 pixel format %d: R%u G%u B%u A%u  depth=%u  flags=0x%08lX\n",
            pfIdx,
            (unsigned)pfd.cRedBits, (unsigned)pfd.cGreenBits,
            (unsigned)pfd.cBlueBits, (unsigned)pfd.cAlphaBits,
            (unsigned)pfd.cDepthBits, pfd.dwFlags);
        ReleaseDC(hwnd, hdc);
    }
}
#endif // _WIN32

static void log_sdl_display_info(SDL_Window *window, const char *label)
{
    LbJustLog("[GL-diag] === SDL display state: %s ===\n", label);
    int wx, wy, ww, wh;
    SDL_GetWindowPosition(window, &wx, &wy);
    SDL_GetWindowSize(window, &ww, &wh);
    Uint32 flags = SDL_GetWindowFlags(window);
    LbJustLog("[GL-diag]   window pos=(%d,%d)  size=%dx%d\n", wx, wy, ww, wh);
    const char *fsMode =
        ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP) ? "DESKTOP" :
        (flags & SDL_WINDOW_FULLSCREEN) ? "EXCLUSIVE" : "none";
    LbJustLog("[GL-diag]   flags=0x%08X  SHOWN=%d HIDDEN=%d OPENGL=%d BORDERLESS=%d FULLSCREEN=%s\n",
        (unsigned)flags,
        (flags & SDL_WINDOW_SHOWN)      ? 1 : 0,
        (flags & SDL_WINDOW_HIDDEN)     ? 1 : 0,
        (flags & SDL_WINDOW_OPENGL)     ? 1 : 0,
        (flags & SDL_WINDOW_BORDERLESS) ? 1 : 0,
        fsMode);
    int dispIdx = SDL_GetWindowDisplayIndex(window);
    LbJustLog("[GL-diag]   display index=%d  (%d displays total)\n",
        dispIdx, SDL_GetNumVideoDisplays());
    if (dispIdx >= 0)
    {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(dispIdx, &bounds) == 0)
            LbJustLog("[GL-diag]   display bounds: (%d,%d)  %dx%d\n",
                bounds.x, bounds.y, bounds.w, bounds.h);
        SDL_DisplayMode dmode;
        if (SDL_GetDesktopDisplayMode(dispIdx, &dmode) == 0)
            LbJustLog("[GL-diag]   desktop mode : %dx%d @ %d Hz  fmt=0x%08X\n",
                dmode.w, dmode.h, dmode.refresh_rate, dmode.format);
        if (SDL_GetCurrentDisplayMode(dispIdx, &dmode) == 0)
            LbJustLog("[GL-diag]   current mode : %dx%d @ %d Hz  fmt=0x%08X\n",
                dmode.w, dmode.h, dmode.refresh_rate, dmode.format);
    }
}

/******************************************************************************/

static SDL_GLContext s_glContext = nullptr;
static SDL_Window*   s_glWindow  = nullptr;

extern "C" int platform_create_gl_context(void *sdl_window)
{
    SDL_Window *window = static_cast<SDL_Window*>(sdl_window);

    // Context attributes — SDL2 uses these in its internal call to
    // wglCreateContextAttribsARB (Windows) or the platform equivalent.
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(MAJOR_VERSION,3) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(MINOR_VERSION,3) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(PROFILE_CORE) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(DOUBLEBUFFER,1) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(DEPTH_SIZE,24) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(STENCIL_SIZE,8) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_RED_SIZE,    8) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(RED_SIZE,8) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,  8) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(GREEN_SIZE,8) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,   8) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(BLUE_SIZE,8) failed: %s\n", SDL_GetError());
    if (SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,  8) != 0)
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(ALPHA_SIZE,8) failed: %s\n", SDL_GetError());

    log_sdl_display_info(window, "before GL context creation");
#ifdef _WIN32
    log_window_diag(window);
#endif

    // SDL2 already called SetPixelFormat + loaded wglCreateContextAttribsARB
    // inside SDL_CreateWindow (WIN_GL_SetupWindow).  SDL_GL_CreateContext
    // uses those to create the 3.3 Core context — no separate WGL bootstrap
    // is needed.
    s_glContext = SDL_GL_CreateContext(window);
    if (!s_glContext)
    {
        LbWarnLog("platform_create_gl_context: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 0;
    }

    if (SDL_GL_MakeCurrent(window, s_glContext) != 0)
    {
        LbWarnLog("platform_create_gl_context: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(s_glContext);
        s_glContext = nullptr;
        return 0;
    }

    // Set no V-sync, it's fucking with frame ordering.
    SDL_GL_SetSwapInterval(0);

    // The window was created with SDL_WINDOW_HIDDEN so that SDL2's internal
    // SetPixelFormat does not trigger a visible DWM composition-pipeline
    // reconfiguration on HDR displays.  Show it now that the GL context is
    // fully configured.
    SDL_ShowWindow(window);

    log_sdl_display_info(window, "after GL context creation");
#ifdef _WIN32
    log_window_diag(window);
#endif

    s_glWindow = window;
    return 1;
}

extern "C" void platform_destroy_gl_context(void)
{
    if (s_glContext)
    {
        SDL_GL_DeleteContext(s_glContext);
        s_glContext = nullptr;
    }
    s_glWindow = nullptr;
}

extern "C" void platform_swap_gl_buffers(void *sdl_window)
{
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(sdl_window));
}

extern "C" void* platform_get_sdl_window(void)
{
    return GetSDLWindowSystem()->GetSDLWindow();
}

extern "C" void platform_gl_release_context(void)
{
    if (SDL_GL_MakeCurrent(s_glWindow, nullptr) != 0)
        LbWarnLog("platform_gl_release_context: SDL_GL_MakeCurrent(null) failed: %s\n", SDL_GetError());
}

extern "C" void platform_gl_acquire_context(void)
{
    if (SDL_GL_MakeCurrent(s_glWindow, s_glContext) != 0)
        LbWarnLog("platform_gl_acquire_context: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
}

extern "C" int platform_is_renderdoc_present(void)
{
#ifdef _WIN32
    // RenderDoc injects renderdoc.dll into the target process before attaching.
    // GetModuleHandleA does NOT load the DLL; it only returns non-NULL if it is
    // already mapped into our address space.
    return GetModuleHandleA("renderdoc.dll") != NULL;
#elif defined(__linux__)
    // On Linux, RenderDoc injects librenderdoc.so.  dlopen with RTLD_NOLOAD
    // returns a handle only if the library is already loaded; it never loads it.
    void* h = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (h) { dlclose(h); return 1; }
    return 0;
#else
    return 0;
#endif
}

extern "C" void* platform_gl_get_proc_address(const char* proc)
{
    return SDL_GL_GetProcAddress(proc);
}
