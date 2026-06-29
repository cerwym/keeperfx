/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file platform_gl_sdl3.cpp
 *     OpenGL context management for SDL3-based desktop platforms.
 * @par Purpose:
 *     Implements platform_create_gl_context / platform_destroy_gl_context /
 *     platform_swap_gl_buffers for any platform using SDL3 as the windowing
 *     layer (Windows, Linux, macOS).
 * @par Windows note:
 *     SDL3 calls SetPixelFormat on the window's DC during SDL_CreateWindow
 *     (WIN_GL_SetupWindow) and loads wglCreateContextAttribsARB at the same
 *     time.  SDL_GL_CreateContext then uses that extension to create the 3.3
 *     Core context.  GL pixel-format attributes MUST be set before the window
 *     is created (via SDL_GL_SetAttribute) so SDL3 picks them up.
 *     The window is created with SDL_WINDOW_HIDDEN so that SetPixelFormat
 *     does not trigger a DWM composition-pipeline reconfiguration on HDR
 *     displays (which would cause a visible black flash).  The window is
 *     shown after the GL context is fully active.
 * @par HDR note:
 *     DWM operates in scRGB on HDR monitors.  SDR apps need an sRGB
 *     framebuffer so Windows tone-maps them correctly instead of applying
 *     a brightness boost.  The 8-bit RGBA pixel format requested here gives
 *     an sRGB swapchain, keeping the game in the SDR range on HDR displays.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform.h"
#include "platform/WindowSystemSDL.h"

#include <SDL3/SDL.h>
#ifdef _WIN32
#  include <dxgi1_6.h>
#  include <dwmapi.h>
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

// Log DXGI HDR state and the pixel format SDL3 selected for the window.
static void log_window_diag(SDL_Window *window)
{
    // SDL3: HWND is retrieved via the properties API (SDL_SysWMinfo removed).
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (!hwnd)
        return;

    log_dxgi_hdr_info(hwnd);

    HDC hdc = GetDC(hwnd);
    if (hdc)
    {
        int pfIdx = GetPixelFormat(hdc);
        PIXELFORMATDESCRIPTOR pfd = {};
        DescribePixelFormat(hdc, pfIdx, sizeof(pfd), &pfd);
        LbJustLog("[GL-diag] SDL3 pixel format %d: R%u G%u B%u A%u  depth=%u  flags=0x%08lX\n",
            pfIdx,
            (unsigned)pfd.cRedBits, (unsigned)pfd.cGreenBits,
            (unsigned)pfd.cBlueBits, (unsigned)pfd.cAlphaBits,
            (unsigned)pfd.cDepthBits, pfd.dwFlags);
        ReleaseDC(hwnd, hdc);
    }
}

// ---------------------------------------------------------------------------
// HDR compositor anchor (Windows only)
// ---------------------------------------------------------------------------
// DXGI can engage "Independent Flip" (direct scanout) when it detects an OpenGL
// swap chain covering the full primary display.  Independent Flip bypasses the
// DWM HDR compositor, switching the monitor from HDR to SDR at the first
// wglSwapBuffers call.  With a second DWM-composited window present, DXGI
// cannot switch to Independent Flip (it requires the game to be the sole
// composited surface), so the DWM HDR compositor remains active.
//
// The anchor is a 1×1 transparent WS_EX_TOPMOST window with alpha=1 — nearly
// invisible but sufficient for DWM to include it in the composition pass.
// WS_EX_TOPMOST keeps it above the game window so DWM's occlusion-culling
// doesn't skip it (the game covers the full display at lower z-order).
// WS_EX_TRANSPARENT lets mouse events fall through to the game window.

static HWND s_hdr_anchor = nullptr;

static LRESULT CALLBACK hdr_anchor_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void hdr_anchor_create(void)
{
    if (s_hdr_anchor)
        return;
    HINSTANCE hInst = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = hdr_anchor_wndproc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "KFX_HDR_Anchor";
    RegisterClassExA(&wc);
    s_hdr_anchor = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        "KFX_HDR_Anchor", nullptr, WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
    if (s_hdr_anchor)
    {
        SetLayeredWindowAttributes(s_hdr_anchor, 0, 1, LWA_ALPHA);
        ShowWindow(s_hdr_anchor, SW_SHOWNA);
        LbJustLog("[GL-diag] HDR anchor: created 1x1 compositor anchor to prevent Independent Flip\n");
    }
    else
    {
        LbJustLog("[GL-diag] HDR anchor: CreateWindowExA failed (error %lu)\n", GetLastError());
    }
}

static void hdr_anchor_destroy(void)
{
    if (s_hdr_anchor)
    {
        DestroyWindow(s_hdr_anchor);
        s_hdr_anchor = nullptr;
    }
}
#endif // _WIN32

static void log_sdl_display_info(SDL_Window *window, const char *label)
{
    LbJustLog("[GL-diag] === SDL display state: %s ===\n", label);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(window, &wx, &wy);
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    LbJustLog("[GL-diag]   window pos=(%d,%d)  size=%dx%d\n", wx, wy, ww, wh);
    // GetWindowFlags() consults m_desktopFakeFullscreen so it correctly reports
    // FAKE_DESKTOP mode even though SDL3 doesn't have SDL_WINDOW_FULLSCREEN set.
    // 0x1001u = SDL_WINDOW_FULLSCREEN_DESKTOP compat value (defined in bflib_video.h,
    // not included here — use the literal to avoid the dependency).
    unsigned int kfx_flags = GetSDLWindowSystem()->GetWindowFlags();
    const char *fsMode =
        (flags & SDL_WINDOW_FULLSCREEN)
            ? (SDL_GetWindowFullscreenMode(window) ? "EXCLUSIVE" : "DESKTOP")
            : ((kfx_flags & 0x1001u) == 0x1001u ? "FAKE_DESKTOP" : "none");
    LbJustLog("[GL-diag]   flags=0x%08X  SHOWN=%d HIDDEN=%d OPENGL=%d BORDERLESS=%d FULLSCREEN=%s\n",
        (unsigned)flags,
        (flags & SDL_WINDOW_HIDDEN) ? 0 : 1,
        (flags & SDL_WINDOW_HIDDEN) ? 1 : 0,
        (flags & SDL_WINDOW_OPENGL) ? 1 : 0,
        (flags & SDL_WINDOW_BORDERLESS) ? 1 : 0,
        fsMode);
    SDL_DisplayID dispId = SDL_GetDisplayForWindow(window);
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays) SDL_free(displays);
    LbJustLog("[GL-diag]   display id=%u  (%d displays total)\n", (unsigned)dispId, count);
    if (dispId != 0)
    {
        SDL_Rect bounds = {0, 0, 0, 0};
        SDL_GetDisplayBounds(dispId, &bounds);
        LbJustLog("[GL-diag]   display bounds: (%d,%d)  %dx%d\n",
            bounds.x, bounds.y, bounds.w, bounds.h);
        const SDL_DisplayMode *dmode = SDL_GetDesktopDisplayMode(dispId);
        if (dmode)
            LbJustLog("[GL-diag]   desktop mode : %dx%d @ %.2f Hz  fmt=0x%08X\n",
                dmode->w, dmode->h, dmode->refresh_rate, (unsigned)dmode->format);
        const SDL_DisplayMode *cmode = SDL_GetCurrentDisplayMode(dispId);
        if (cmode)
            LbJustLog("[GL-diag]   current mode : %dx%d @ %.2f Hz  fmt=0x%08X\n",
                cmode->w, cmode->h, cmode->refresh_rate, (unsigned)cmode->format);
    }
    // Per-window HDR state (different from the display's DXGI colorspace above).
    // SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN is true when the DWM HDR compositor
    // is active for this window — exactly what we want to preserve.
    SDL_PropertiesID winProps = SDL_GetWindowProperties(window);
    bool   winHdr      = SDL_GetBooleanProperty(winProps, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
    float  sdrWhite    = SDL_GetFloatProperty(winProps, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f);
    float  hdrHeadroom = SDL_GetFloatProperty(winProps, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f);
    LbJustLog("[GL-diag]   window HDR_enabled=%d  SDR_white=%.3f  HDR_headroom=%.3f%s\n",
        winHdr ? 1 : 0, sdrWhite, hdrHeadroom,
        winHdr ? "  [HDR ACTIVE for this window]" : "  [SDR mode for this window]");
}

/******************************************************************************/

static SDL_GLContext s_glContext = nullptr;
static SDL_Window*   s_glWindow  = nullptr;

extern "C" int platform_create_gl_context(void *sdl_window)
{
    SDL_Window *window = static_cast<SDL_Window*>(sdl_window);

    // Context attributes must be set BEFORE the GL context is created.
    // In SDL3 these still control wglCreateContextAttribsARB on Windows.
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(MAJOR_VERSION,3) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(MINOR_VERSION,3) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(PROFILE_CORE) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(DOUBLEBUFFER,1) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(DEPTH_SIZE,24) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(STENCIL_SIZE,8) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_RED_SIZE,    8))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(RED_SIZE,8) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,  8))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(GREEN_SIZE,8) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,   8))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(BLUE_SIZE,8) failed: %s\n", SDL_GetError());
    if (!SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,  8))
        LbWarnLog("platform_create_gl_context: SDL_GL_SetAttribute(ALPHA_SIZE,8) failed: %s\n", SDL_GetError());

    log_sdl_display_info(window, "before GL context creation");
#ifdef _WIN32
    log_window_diag(window);
#endif

    s_glContext = SDL_GL_CreateContext(window);
    if (!s_glContext)
    {
        LbWarnLog("platform_create_gl_context: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 0;
    }

    if (!SDL_GL_MakeCurrent(window, s_glContext))
    {
        LbWarnLog("platform_create_gl_context: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DestroyContext(s_glContext);
        s_glContext = nullptr;
        return 0;
    }

    SDL_GL_SetSwapInterval(0);

    // Window was created hidden so SetPixelFormat doesn't cause a DWM
    // composition pipeline reconfiguration on HDR displays.  Show it now.
    SDL_ShowWindow(window);

#ifdef _WIN32
    // Prevent DXGI from engaging "Independent Flip" (direct scanout) which
    // bypasses the DWM HDR compositor and switches the monitor to SDR mode.
    //
    // Two complementary techniques:
    //   1. DwmExtendFrameIntoClientArea with negative margins — tells DWM to
    //      extend its composition frame over the entire client area.  DXGI
    //      checks for this flag before engaging Independent Flip and backs off
    //      when it is set.  This is the same approach used by Chrome / Electron
    //      to maintain DWM compositing on borderless windows.
    //   2. A tiny WS_EX_TOPMOST WS_EX_LAYERED anchor window — provides a
    //      second DWM-composited surface so DXGI does not see the game as the
    //      sole consumer of the display output (belt-and-suspenders).
    if ((GetSDLWindowSystem()->GetWindowFlags() & 0x1001u) == 0x1001u)
    {
        SDL_PropertiesID winProps = SDL_GetWindowProperties(window);
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            winProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd)
        {
            MARGINS m = { -1, -1, -1, -1 };
            HRESULT hr = DwmExtendFrameIntoClientArea(hwnd, &m);
            LbJustLog("[GL-diag] HDR: DwmExtendFrameIntoClientArea -> 0x%08lX (%s)\n",
                (unsigned long)hr,
                SUCCEEDED(hr) ? "forcing DWM compositing" : "failed");
        }
        hdr_anchor_create();
    }
#endif

    log_sdl_display_info(window, "after GL context creation");
#ifdef _WIN32
    log_window_diag(window);
#endif

    s_glWindow = window;
    return 1;
}

extern "C" void platform_destroy_gl_context(void)
{
#ifdef _WIN32
    hdr_anchor_destroy();
#endif
    if (s_glContext)
    {
        SDL_GL_DestroyContext(s_glContext);
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

extern "C" void* platform_get_gl_context(void)
{
    return s_glContext;
}

extern "C" void platform_gl_release_context(void)
{
    if (!SDL_GL_MakeCurrent(s_glWindow, nullptr))
        LbWarnLog("platform_gl_release_context: SDL_GL_MakeCurrent(null) failed: %s\n", SDL_GetError());
}

extern "C" void platform_gl_acquire_context(void)
{
    if (!SDL_GL_MakeCurrent(s_glWindow, s_glContext))
        LbWarnLog("platform_gl_acquire_context: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
}

extern "C" int platform_is_renderdoc_present(void)
{
#ifdef _WIN32
    return GetModuleHandleA("renderdoc.dll") != NULL;
#elif defined(__linux__)
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
