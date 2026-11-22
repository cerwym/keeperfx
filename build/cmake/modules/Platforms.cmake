# Platforms.cmake - platform detection + directory-scope compile definitions.

if(WIN32)
    kfx_status("PLATFORM" "Windows / MinGW-w64 (i686)")
elseif(UNIX AND NOT APPLE)
    kfx_status("PLATFORM" "Linux (x86_64)")
else()
    message(FATAL_ERROR "Unsupported platform (only Windows/MinGW and Linux are supported)")
endif()

add_compile_definitions("DEBUG=$<IF:$<CONFIG:Debug>,1,0>")

option(KEEPERFX_TRACY "Enable Tracy profiler instrumentation" OFF)

option(KFX_SPLIT_DEBUG
    "Linux: move debug info out of the binaries into <binary>.debug files (always on for MinGW)" OFF)
# ThreadSanitizer needs GCC or Clang on Linux; it has no effect elsewhere.
option(KEEPERFX_TSAN "Enable ThreadSanitizer (Linux only)" OFF)

# Static-linkage defines for the prebuilt Windows dependencies.
if(WIN32)
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
    # AL_LIBTYPE_STATIC tells <AL/al.h> not to mark its functions dllimport.
    # Needed when linking the kfx-deps static prebuilt (mirrors the Makefile);
    # not needed -- and not defined -- when vcpkg's openal-soft is in play,
    # which is linked dynamically. See Dependencies.cmake for why the two
    # differ.
    if(NOT VCPKG_TOOLCHAIN)
        add_compile_definitions(AL_LIBTYPE_STATIC)
    endif()
    add_compile_definitions(SPNG_STATIC=1)
    # MSVC's <math.h> only defines M_PI/M_E/etc. when this is set before the
    # first include; MinGW's <math.h> defines them unconditionally regardless,
    # so this is a no-op there. Directory-scope since math.h can be pulled in
    # transitively in an unpredictable order across source files.
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

# ━━━ ImGui Debug/Settings Overlay (optional, desktop + vcpkg only) ━━━
# NOTE: a future homebrew/console platform module sets KEEPERFX_IMGUI OFF
# before this runs (PLATFORM_VITA/PLATFORM_3DS/PLATFORM_SWITCH/PLATFORM_WII_U
# don't exist yet upstream -- this is dormant until one does). The vcpkg-only
# acquisition path means it's also unavailable on the non-vcpkg kfx-deps
# dependency path (see Dependencies.cmake): there's no kfx-deps tarball for
# imgui, only the vcpkg port.
option(KEEPERFX_IMGUI "Enable ImGui debug/settings overlay (desktop only, requires OpenGL)" ON)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH OR PLATFORM_WII_U)
    set(KEEPERFX_IMGUI OFF CACHE BOOL "ImGui not available on homebrew" FORCE)
endif()
if(NOT VCPKG_TOOLCHAIN)
    set(KEEPERFX_IMGUI OFF CACHE BOOL "ImGui requires the vcpkg toolchain (not available via the kfx-deps prebuilt path)" FORCE)
endif()
if(KEEPERFX_IMGUI)
    kfx_status("PLATFORM" "ImGui debug/settings overlay enabled")
else()
    kfx_status("PLATFORM" "ImGui debug/settings overlay disabled")
endif()
