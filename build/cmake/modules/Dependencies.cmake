# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Dependencies.cmake — find_package() for all dependencies
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# Desktop platforms (MinGW cross-compile or native Linux/macOS) use vcpkg or system packages
# Homebrew platforms (Vita, 3DS, Switch) use their own SDK bundled dependencies
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    # ━━━ SDL2 & Graphics ━━━
    # MinGW cross-compile: force static SDL2 libs (no runtime DLLs)
    if(MINGW OR CMAKE_CROSSCOMPILING)
        set(SDL2_USE_STATIC_LIBS ON)
        set(SDL2IMAGE_STATIC ON)
        set(SDL2MIXER_STATIC ON)
        set(SDL2NET_STATIC ON)
    endif()
    
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    if(KEEPERFX_NETWORKING)
        find_package(SDL2_net CONFIG REQUIRED)
    endif()

    # ━━━ OpenGL Renderer (optional) ━━━
    if(KEEPERFX_RENDERER_OPENGL)
        find_package(glad CONFIG QUIET)
        if(glad_FOUND)
            kfx_status("DEPS" "OpenGL renderer backend enabled (glad found)")
            add_compile_definitions(RENDERER_OPENGL_ENABLED=1)
        else()
            kfx_status("DEPS" "OpenGL renderer backend disabled (glad not found; install via vcpkg)")
            set(KEEPERFX_RENDERER_OPENGL OFF)
        endif()
    endif()
    
    # ━━━ Audio & Codecs (vcpkg) ━━━
    find_package(FFmpeg MODULE QUIET)
    if(FFmpeg_FOUND)
        kfx_status("DEPS" "FFmpeg found (vcpkg)")
    else()
        kfx_status("DEPS" "FFmpeg not yet in vcpkg — will use fallback from deps/ tarballs")
    endif()
    
    find_package(OpenAL CONFIG QUIET)
    if(OpenAL_FOUND OR OPENAL_FOUND)
        kfx_status("DEPS" "OpenAL found (vcpkg)")
    else()
        kfx_status("DEPS" "OpenAL not found — will use fallback from deps/ tarballs")
    endif()
    
    # ━━━ Networking ━━━
    find_package(enet CONFIG QUIET)
    if(enet_FOUND)
        kfx_status("DEPS" "enet found (vcpkg)")
    else()
        kfx_status("DEPS" "FFmpeg not yet in vcpkg — will use fallback from deps/ tarballs")
    endif()

    # ━━━ JSON (centijson) ━━━
    find_package(centijson CONFIG QUIET)
    if(centijson_FOUND)
        kfx_status("DEPS" "centijson found (vcpkg)")
    else()
        kfx_status("DEPS" "centijson not found via vcpkg — will build from deps/ sources")
    endif()

    # ━━━ Tracy Profiler (optional) — built from source via FetchContent ━━━
    # Using FetchContent instead of vcpkg so Tracy is compiled with the same CRT
    # as the rest of the project (MTd in Debug, MT in Release). The vcpkg
    # pre-built lib uses MT regardless, causing LNK2038 CRT mismatch in Debug.
    if(KEEPERFX_TRACY)
        include(FetchContent)
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy.git
            GIT_TAG        v0.13.1
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )
        FetchContent_MakeAvailable(tracy)
        kfx_status("PROFILER" "Tracy profiler v0.13.1 fetched from source (TRACY_ENABLE + TRACY_ON_DEMAND)")
    endif()
    
else()
    # ━━━ Homebrew platforms (Vita, 3DS, Switch) ━━━
    kfx_status("DEPS" "Skipping vcpkg — using platform SDK dependencies")
    
    if(PLATFORM_VITA)
        # SDL2 from vitasdk
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VITASDK}/arm-vita-eabi")
        find_package(SDL2 REQUIRED)
    endif()
endif()

# ━━━ Global Definitions (all platforms) ━━━
add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
add_compile_definitions("DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
add_compile_definitions("SPNG_STATIC=1")
