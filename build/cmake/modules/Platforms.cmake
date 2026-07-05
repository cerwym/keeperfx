# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Platforms.cmake — Platform detection (Vita, 3DS, Switch, Wii U, desktop)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# Platform detection — toolchain variables take precedence, explicit flags override autodetect
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH AND NOT PLATFORM_WII_U)
    if(VITA OR CMAKE_SYSTEM_NAME STREQUAL "vita")
        set(PLATFORM_VITA ON CACHE BOOL "Build for PlayStation Vita")
    elseif(NINTENDO_3DS OR CMAKE_SYSTEM_NAME STREQUAL "3DS")
        set(PLATFORM_3DS ON CACHE BOOL "Build for Nintendo 3DS")
    elseif(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        set(PLATFORM_SWITCH ON CACHE BOOL "Build for Nintendo Switch")
    elseif(NINTENDO_WII_U OR CMAKE_SYSTEM_NAME STREQUAL "WiiU")
        set(PLATFORM_WII_U ON CACHE BOOL "Build for Nintendo Wii U")
    elseif(DEFINED ENV{VITASDK} AND NOT DEFINED ENV{DEVKITPRO})
        set(PLATFORM_VITA ON CACHE BOOL "Build for PlayStation Vita")
        kfx_status("PLATFORM" "Auto-detected VITASDK environment")
    endif()
endif()

# Print platform info and set compile definitions
if(PLATFORM_VITA)
    message(STATUS "Building for PlayStation Vita")
    add_compile_definitions(PLATFORM_VITA=1)
elseif(PLATFORM_3DS)
    message(STATUS "Building for Nintendo 3DS")
    add_compile_definitions(PLATFORM_3DS=1)
elseif(PLATFORM_SWITCH)
    message(STATUS "Building for Nintendo Switch")
    add_compile_definitions(PLATFORM_SWITCH=1)
elseif(PLATFORM_WII_U)
    message(STATUS "Building for Nintendo Wii U")
    add_compile_definitions(PLATFORM_WII_U=1)
else()
    message(STATUS "Building for desktop platform")
endif()

# Platform-specific options
option(KEEPERFX_NETWORKING "Build with multiplayer networking support" ON)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH OR PLATFORM_WII_U)
    set(KEEPERFX_NETWORKING OFF CACHE BOOL "Networking disabled on homebrew" FORCE)
endif()

option(KEEPERFX_RENDERER_OPENGL "Enable the OpenGL renderer backend (desktop only)" ON)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH OR PLATFORM_WII_U)
    set(KEEPERFX_RENDERER_OPENGL OFF CACHE BOOL "OpenGL not available on homebrew" FORCE)
endif()

option(KEEPERFX_IMGUI "Enable ImGui debug/settings overlay (desktop only, requires OpenGL)" ON)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH OR PLATFORM_WII_U)
    set(KEEPERFX_IMGUI OFF CACHE BOOL "ImGui not available on homebrew" FORCE)
endif()
if(KEEPERFX_IMGUI)
    kfx_status("PLATFORM" "ImGui debug/settings overlay enabled")
else()
    kfx_status("PLATFORM" "ImGui debug/settings overlay disabled")
endif()

option(KFX_DEBUG_MEMORY "Enable KfxAlloc guard zones and per-site tracking" OFF)

# Vita-specific options
if(PLATFORM_VITA)
    option(VITA_PERF_LOG "Enable startup timing logs on Vita (debug only)" OFF)
    option(VITA_SCE_DIAG "Enable taiHEN SCE subsystem diagnostic hooks (vita-debug only)" OFF)
endif()

# Apply platform-specific compile definitions
if(KEEPERFX_NETWORKING)
    add_compile_definitions("KEEPERFX_NETWORKING=1")
endif()

if(KFX_DEBUG_MEMORY)
    add_compile_definitions("KFX_DEBUG_MEMORY=1")
endif()

if(PLATFORM_VITA)
    if(VITA_PERF_LOG)
        add_compile_definitions("VITA_PERF_LOG=1")
    endif()
    if(VITA_SCE_DIAG)
        add_compile_definitions("VITA_SCE_DIAG=1")
    endif()
endif()

# LuaJIT availability per platform
if(NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH AND NOT PLATFORM_WII_U)
    add_compile_definitions("KEEPERFX_LUA_AVAILABLE=1")
endif()

# SDL2_mixer available on desktop only
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH AND NOT PLATFORM_WII_U)
    add_compile_definitions("SDL_MIXER_AVAILABLE=1")
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# MSVC Hot Reload (Edit and Continue)
# Replace CMake's default /Zi with /ZI so the VS debugger can apply code changes
# without a full restart.  Only affects Debug; Release keeps /Zi via RelWithDebInfo.
# Skipped when ASAN is enabled: /ZI (Edit and Continue) is incompatible with
# /fsanitize=address — ASAN requires /Zi and /INCREMENTAL:NO.
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(MSVC AND NOT KEEPERFX_SANITIZERS)
    foreach(lang C CXX)
        string(REPLACE "/Zi" "/ZI" CMAKE_${lang}_FLAGS_DEBUG "${CMAKE_${lang}_FLAGS_DEBUG}")
        set(CMAKE_${lang}_FLAGS_DEBUG "${CMAKE_${lang}_FLAGS_DEBUG}" CACHE STRING "" FORCE)
    endforeach()
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
option(KEEPERFX_TRACY "Enable Tracy profiler instrumentation (MSVC debug builds)" OFF)

option(KEEPERFX_SANITIZERS "Enable AddressSanitizer (+ UBSan on GCC/Clang)" OFF)
# ASan requires shadow memory (~1:8 address space ratio). On constrained 32-bit
# platforms (Vita/3DS/Switch) the ~32 MB shadow region would exhaust the process
# address space immediately. Silently skip rather than producing a misleading build.
if(KEEPERFX_SANITIZERS AND NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    if(MSVC)
        add_compile_options(/fsanitize=address)
        # MSVC ASan needs the debug info for useful stack traces
        add_compile_options(/Zi)
        add_link_options(/DEBUG)
        # Disable STL container annotations — pre-built vcpkg libs don't have them,
        # causing LNK2038 mismatch errors (annotate_string / annotate_vector)
        add_compile_definitions(_DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
        kfx_status("SANITIZERS" "MSVC AddressSanitizer enabled (/fsanitize=address)")
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        kfx_status("SANITIZERS" "GCC/Clang ASan + UBSan enabled")
    endif()
endif()
