cmake_minimum_required(VERSION 3.20)

# Do not allow building in root
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Do not build in the root. Create a bin directory and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif ()

project(keeperfx C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(VER_MAJOR       1)
set(VER_MINOR       3)
set(VER_RELEASE     1)
set(VER_BUILD       0)
set(VER_STRING      "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${VER_BUILD} ${PACKAGE_SUFFIX}")
set(PACKAGE_SUFFIX  "")

# CMAKE_BINARY_DIR is defined in CMakePresets.json.
set(KEEPERFX_VER_DEFS_H_IN ${CMAKE_SOURCE_DIR}/build/ver_defs.h.in)
set(KEEPERFX_VER_DEFS_H_OUT ${CMAKE_SOURCE_DIR}/src/ver_defs.h)
configure_file(${KEEPERFX_VER_DEFS_H_IN} ${KEEPERFX_VER_DEFS_H_OUT})

# Platform detection - toolchain variables take precedence, explicit flags override autodetect
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    if(VITA OR CMAKE_SYSTEM_NAME STREQUAL "vita")
        set(PLATFORM_VITA ON CACHE BOOL "Build for PlayStation Vita")
    elseif(NINTENDO_3DS OR CMAKE_SYSTEM_NAME STREQUAL "3DS")
        set(PLATFORM_3DS ON CACHE BOOL "Build for Nintendo 3DS")
    elseif(NINTENDO_SWITCH OR CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        set(PLATFORM_SWITCH ON CACHE BOOL "Build for Nintendo Switch")
    elseif(DEFINED ENV{VITASDK} AND NOT DEFINED ENV{DEVKITPRO})
        set(PLATFORM_VITA ON CACHE BOOL "Build for PlayStation Vita")
        message(STATUS "Auto-detected VITASDK environment")
    endif()
endif()

if(PLATFORM_VITA)
    message(STATUS "Building for PlayStation Vita")
    add_compile_definitions(PLATFORM_VITA=1)
elseif(PLATFORM_3DS)
    message(STATUS "Building for Nintendo 3DS")
    add_compile_definitions(PLATFORM_3DS=1)
elseif(PLATFORM_SWITCH)
    message(STATUS "Building for Nintendo Switch")
    add_compile_definitions(PLATFORM_SWITCH=1)
else()
    message(STATUS "Building for desktop platform")
endif()

# Dependencies: platform SDKs provide their own; desktop uses vcpkg or find_package
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    # When cross-compiling for Windows with MinGW, force static SDL2 libs so the
    # final exe doesn't require SDL2*.dll at runtime.
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

    # Optional: OpenGL renderer backend (requires glad via vcpkg)
    option(KEEPERFX_RENDERER_OPENGL "Enable the OpenGL renderer backend" ON)
    if(KEEPERFX_RENDERER_OPENGL)
        find_package(glad CONFIG QUIET)
        if(glad_FOUND)
            message(STATUS "OpenGL renderer backend enabled (glad found)")
        else()
            message(STATUS "OpenGL renderer backend disabled (glad not found; install via vcpkg)")
            set(KEEPERFX_RENDERER_OPENGL OFF)
        endif()
    endif()
else()
    # Homebrew platforms: SDL2 comes from the platform SDK
    message(STATUS "Skipping vcpkg dependency resolution for homebrew platform")
endif()

# Global definitions.
add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)

file(GLOB_RECURSE KEEPERFX_SOURCES_C "src/*.c")
file(GLOB_RECURSE KEEPERFX_SOURCES_CXX "src/*.cpp")

# Exclude stub files from the glob — they are added back explicitly when needed
list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_cpu_stub\\.c$")
list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_network_stub\\.c$")
# debugscreen is Vita-only; always exclude from glob (added back explicitly in PLATFORM_VITA block)
list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/debugscreen/.*\\.c$")
# RendererOpenGL requires glad; exclude when not available
if(NOT KEEPERFX_RENDERER_OPENGL)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/RendererOpenGL\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform_gl_sdl2\\.cpp$")
endif()

# Networking: disable for homebrew platforms by default; can also be disabled on desktop.
option(KEEPERFX_NETWORKING "Build with multiplayer networking support" ON)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    set(KEEPERFX_NETWORKING OFF CACHE BOOL "Networking disabled on homebrew" FORCE)
endif()

if(KEEPERFX_NETWORKING)
    add_compile_definitions("KEEPERFX_NETWORKING=1")
else()
    # Exclude all networking source files including API server (stubs provided in platform_shims.c)
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/api\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_tcpsp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_base_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_client_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_server_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_enet\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_portforward\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_netsession\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_netsp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_network\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_network_exchange\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_resync\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_input_lag\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_received_packets\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_redundant_packets\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_checksums\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_game\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_enet\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_base_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_client_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_server_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/net_portforward\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/net_resync\\.cpp$")
    # Add the networking stub that provides no-op implementations
    list(APPEND KEEPERFX_SOURCES_C "src/bflib_network_stub.c")
endif()

# On homebrew platforms, exclude desktop-only source files
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/audio/audio_openal\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/input/input_sdl\\.c$")
    # On non-x86 platforms, replace bflib_cpu.c with the portable stub
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_cpu\\.c$")
    list(APPEND KEEPERFX_SOURCES_C "src/bflib_cpu_stub.c")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_fmvids\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/KeeperSpeechImp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/steam_api\\.c$")
    # custom_sprites.c is enabled on Vita (spng/minizip/zlib built from source via FetchContent — see PLATFORM_VITA block below).
    # TODO(3DS/Switch): replicate the same FetchContent pattern for the devkitARM/devkitA64 toolchain to lift this exclusion.
    if(NOT PLATFORM_VITA)
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/custom_sprites\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/custom_sprites_cache\\.c$")
    endif()
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/moonphase\\.c$")
    # Lua: available on Vita via SonicMastr/LuaJIT-Vita (vdpm). Not yet available on 3DS/Switch.
    if(NOT PLATFORM_VITA)
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_api.*\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_base\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_cfg_funcs\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_params\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_triggers\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_utils\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/console_cmd\\.c$")
    endif()
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/scrcapt\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_sndlib\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_fmvids\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/cdrom\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/linux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/steam_api\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/windows\\.cpp$")
    # Exclude desktop-only IPlatform implementations
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    # Exclude IPlatform implementations for the other homebrew platforms
    if(NOT PLATFORM_VITA)
        list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformVita\\.cpp$")
    endif()
    if(NOT PLATFORM_3DS)
        list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/Platform3DS\\.cpp$")
    endif()
    if(NOT PLATFORM_SWITCH)
        list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformSwitch\\.cpp$")
    endif()
    list(APPEND KEEPERFX_SOURCES_C "src/platform_shims.c")
endif()

# On Vita, FMV support is provided by FFmpeg-vita — re-add bflib_fmvids.cpp that was excluded
# in the general homebrew block above.  The find_library calls and target_link_libraries for
# the FFmpeg libraries live in the PLATFORM_VITA external-libraries block further below.
if(PLATFORM_VITA)
    list(APPEND KEEPERFX_SOURCES_CXX "src/bflib_fmvids.cpp")
    # psvDebugScreen: minimal framebuffer text renderer (from vitaSDK samples).
    # Used for pre-vitaGL fatal error messages so the user sees an on-screen
    # message rather than a silent crash or a log file entry.
    # debugScreenFont.c is #include'd by debugScreen.c; exclude the font file
    # from the glob to prevent a double-compilation, and suppress warnings that
    # originate in this third-party code.
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/debugscreen/debugScreenFont\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/debugscreen/debugScreen\\.c$")
    list(APPEND KEEPERFX_SOURCES_C "src/platform/debugscreen/debugScreen.c")
    set_source_files_properties("src/platform/debugscreen/debugScreen.c"
        PROPERTIES COMPILE_FLAGS "-Wno-shadow")
endif()

# Global definitions for all targets.
add_compile_definitions("DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
add_compile_definitions("SPNG_STATIC=1")
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    add_compile_definitions("SDL_MIXER_AVAILABLE=1")
endif()
# LuaJIT is available on Vita (SonicMastr/LuaJIT-Vita, installed via vdpm).
# 3DS and Switch do not have a LuaJIT port yet.
if(NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    add_compile_definitions("KEEPERFX_LUA_AVAILABLE=1")
endif()

# Build centijson from source for homebrew platforms (prebuilt libjson.a is MinGW32 only)
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    add_library(centijson_static STATIC
        deps/centijson/json.c
        deps/centijson/json-dom.c
        deps/centijson/json-ptr.c
        deps/centijson/value.c)
    target_include_directories(centijson_static PUBLIC deps/centijson/include)
    add_library(centitoml OBJECT deps/centitoml/toml_api.c)
    target_include_directories(centitoml INTERFACE deps/centitoml)
    target_link_libraries(centitoml PUBLIC centijson_static)
endif()

# Platform-specific IPlatform implementation filtering for desktop builds
if(WIN32 OR MINGW)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    # linux.cpp provides Linux-only stubs that conflict with Windows-native cdrom.cpp + steam_api.cpp
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/linux\\.cpp$")
elseif(UNIX AND NOT APPLE)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    # windows.cpp provides Windows-only stubs; exclude on Linux
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/windows\\.cpp$")
elseif(APPLE)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/linux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/windows\\.cpp$")
endif()

# Add two executable targets: keeperfx and keeperfx_hvlog.
add_executable(keeperfx ${KEEPERFX_SOURCES_C} ${KEEPERFX_SOURCES_CXX})
target_include_directories(keeperfx PRIVATE src deps/centitoml deps/centijson/include)

target_compile_definitions(keeperfx PUBLIC BFDEBUG_LEVEL=0)
if(WIN32)
    target_sources(keeperfx PRIVATE "res/keeperfx_stdres.rc")
endif()

add_executable(keeperfx_hvlog ${KEEPERFX_SOURCES_C} ${KEEPERFX_SOURCES_CXX})
target_include_directories(keeperfx_hvlog PRIVATE src deps/centitoml deps/centijson/include)

target_compile_definitions(keeperfx_hvlog PUBLIC BFDEBUG_LEVEL=10)
if(WIN32)
    target_sources(keeperfx_hvlog PRIVATE "res/keeperfx_stdres.rc")
endif()

# Test executable with CUnit — desktop only
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    file(GLOB TEST_SOURCES "tests/*.cpp")
    add_library(cunit_static STATIC
        "deps/CUnit-2.1-3/CUnit/Sources/Basic/Basic.c"
        "deps/CUnit-2.1-3/CUnit/Sources/Framework/TestDB.c"
        "deps/CUnit-2.1-3/CUnit/Sources/Framework/CUError.c"
        "deps/CUnit-2.1-3/CUnit/Sources/Framework/TestRun.c"
        "deps/CUnit-2.1-3/CUnit/Sources/Framework/Util.c"
    )
    target_include_directories(cunit_static PUBLIC "deps/CUnit-2.1-3/CUnit/Headers")

    add_executable(tests ${TEST_SOURCES} ${KEEPERFX_SOURCES_C} ${KEEPERFX_SOURCES_CXX})
    target_compile_definitions(tests PUBLIC BFDEBUG_LEVEL=0)
    target_link_libraries(tests PRIVATE cunit_static)
endif()

message(STATUS "We are using ${CMAKE_CXX_COMPILER_ID}")

# The default bfd linker in MinGW is extremely slow. LLVM linker (LLD) is much much faster.
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    set_property(TARGET keeperfx PROPERTY LINKER_TYPE LLD)
    set_property(TARGET keeperfx_hvlog PROPERTY LINKER_TYPE LLD)
    set_property(TARGET tests PROPERTY LINKER_TYPE LLD)
endif()

set(WARNFLAGS -Wall -W -Wshadow -Wno-sign-compare -Wno-unused-parameter -Wno-strict-aliasing -Wno-unknown-pragmas -Werror)

if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    # Homebrew platforms: ARM/AArch64 cross-compile, no x86 flags, no Windows subsystem
    target_compile_options(keeperfx PRIVATE ${WARNFLAGS} -Wno-format-truncation -Wno-char-subscripts)
    target_compile_options(keeperfx_hvlog PRIVATE ${WARNFLAGS} -Wno-format-truncation -Wno-char-subscripts)
else()
    set(GNU_COMPILER_FLAG -march=x86-64 -fno-omit-frame-pointer -fmessage-length=0)
    set(GNU_LINK_FLAG -mwindows -Wl,--enable-auto-import)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wimplicit")
    target_compile_options(keeperfx PRIVATE ${WARNFLAGS} ${GNU_COMPILER_FLAG})
    target_compile_options(keeperfx_hvlog PRIVATE ${WARNFLAGS} ${GNU_COMPILER_FLAG})
    target_compile_options(tests PRIVATE ${WARNFLAGS} ${GNU_COMPILER_FLAG})
    target_link_options(keeperfx PRIVATE ${GNU_LINK_FLAG} -Wl,-Map,keeperfx.map)
    target_link_options(keeperfx_hvlog PRIVATE ${GNU_LINK_FLAG} -Wl,-Map,keeperfx_hvlog.map)
    target_link_options(tests PRIVATE ${GNU_LINK_FLAG} -Wl,-Map,tests.map)
    target_link_libraries(keeperfx PUBLIC -static stdc++ winpthread -dynamic)
    target_link_libraries(keeperfx_hvlog PUBLIC -static stdc++ winpthread -dynamic)
    target_link_libraries(tests PUBLIC -static stdc++ winpthread -dynamic)
endif()

# System libraries (Windows only).
if(WIN32)
    target_link_libraries(keeperfx PRIVATE imagehlp dbghelp ole32 uuid winmm ws2_32)
    target_link_libraries(keeperfx_hvlog PRIVATE imagehlp dbghelp ole32 uuid winmm ws2_32)
    target_link_libraries(tests PRIVATE imagehlp dbghelp ole32 uuid winmm ws2_32)
endif()

# Go into submodules (desktop only — homebrew platforms use their own SDK deps).
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    add_subdirectory(deps)
    add_subdirectory(tools)
endif()

# External libraries.
if(PLATFORM_VITA)
    # SDL2 cmake config is installed under the vitasdk sysroot
    list(APPEND CMAKE_PREFIX_PATH "$ENV{VITASDK}/arm-vita-eabi")
    find_package(SDL2 REQUIRED)
    target_link_libraries(keeperfx PRIVATE SDL2::SDL2)
    target_link_libraries(keeperfx_hvlog PRIVATE SDL2::SDL2)
    # Cortex-A9 NEON: enable the SIMD unit and allow the compiler to auto-vectorise
    # eligible loops.  -ffast-math also enables faster sin/cos/sqrt paths when
    # mathneon is linked.  These flags are ARM/Vita-only and have no effect on desktop.
    target_compile_options(keeperfx       PRIVATE -mcpu=cortex-a9 -mfpu=neon -ffast-math)
    target_compile_options(keeperfx_hvlog PRIVATE -mcpu=cortex-a9 -mfpu=neon -ffast-math)
    # Guarantee DWARF-4 debug info for crash analysis (vita_crash tool, pyelftools)
    # and GDB remote debugging (kvdb/vdbtcp). CMake adds -g for Debug/RelWithDebInfo
    # but does not specify the DWARF version — make it explicit.
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(keeperfx       PRIVATE -gdwarf-4)
        target_compile_options(keeperfx_hvlog PRIVATE -gdwarf-4)
    endif()
    # vitaGL palette shader renderer — disabled by default because SDL2's GXM
    # driver calls sceGxmInitialize at SDL_Init(VIDEO) time, which causes
    # vglInitExtended to fail (GXM_ERROR_ALREADY_INITIALIZED). To use vitaGL,
    # the initialisation order must be restructured (vitaGL → SDL), which is a
    # larger refactor. Enable with -DVITA_ENABLE_VITAGL=ON once that is done.
    option(VITA_ENABLE_VITAGL "Enable vitaGL GPU palette shader on Vita" OFF)
    # Enable startup/load timing logs in keeperfx.log (debug builds only)
    option(VITA_PERF_LOG "Enable startup timing logs on Vita (debug only)" OFF)
    if(VITA_PERF_LOG)
        target_compile_definitions(keeperfx PUBLIC VITA_PERF_LOG=1)
    endif()
    if(VITA_ENABLE_VITAGL)
        include(ExternalProject)
        # vitashark (GLSL shader compiler) and mathneon (NEON math) must be
        # pre-installed via vdpm; vitashark headers are also needed at vitaGL
        # compile time.  Install once with:
        #   $VITASDK/bin/vdpm vitashark mathneon
        find_library(VITASHARK_LIB vitashark HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
        find_library(MATHNEON_LIB  mathneon  HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
        if(NOT VITASHARK_LIB OR NOT MATHNEON_LIB)
            message(FATAL_ERROR
                "vitashark/mathneon not found.\n"
                "Install via:  $VITASDK/bin/vdpm vitashark mathneon")
        endif()
        message(STATUS "vitashark found: ${VITASHARK_LIB}")
        message(STATUS "mathneon found:  ${MATHNEON_LIB}")

        # vitaGL is always built from source so the debug flags below are baked
        # in.  Flags are kept in sync with .github/workflows/ci-homebrew.yml.
        # To force a rebuild, delete: ${CMAKE_BINARY_DIR}/_deps/vitagl-src/
        # DEBUG_GLSL_TRANSLATOR/PREPROCESSOR log GLSL→CG translation; they
        # require HAVE_GLSL_SUPPORT=1 to activate those code paths.
        set(VITAGL_INSTALL_LIB "$ENV{VITASDK}/arm-vita-eabi/lib/libvitaGL.a")
        ExternalProject_Add(vitagl_external
            GIT_REPOSITORY  https://github.com/Rinnegatamante/vitaGL.git
            GIT_TAG         e6011789a48be7f483b840e5e276f50253c06604  # 2026-02-16: "Add support for partial uniform array uploads" — last commit before vglDrawObjects moved behind ENABLE_LEGACY_PIPELINE
            GIT_SHALLOW     FALSE  # shallow clone requires a branch/tag; disabled when pinning to a commit hash
            SOURCE_DIR      ${CMAKE_BINARY_DIR}/_deps/vitagl-src
            BUILD_IN_SOURCE TRUE
            CONFIGURE_COMMAND ""
            PATCH_COMMAND   git reset --hard HEAD && git clean -fd
            # Remove any pre-installed vitaGL first to avoid version conflicts
            BUILD_COMMAND   /bin/bash -c "rm -f $ENV{VITASDK}/arm-vita-eabi/lib/libvitaGL.a $ENV{VITASDK}/arm-vita-eabi/include/vitaGL.h $ENV{VITASDK}/arm-vita-eabi/include/imgui_impl_vitagl.h && make LOG_ERRORS=2 HAVE_RAZOR=1 HAVE_PROFILING=0 HAVE_DEBUGGER=1 DEBUG_GLSL_TRANSLATOR=1 DEBUG_GLSL_PREPROCESSOR=1 HAVE_GLSL_SUPPORT=1"
            INSTALL_COMMAND /bin/bash -c "make install LOG_ERRORS=2 HAVE_RAZOR=1 HAVE_PROFILING=0 HAVE_DEBUGGER=1 DEBUG_GLSL_TRANSLATOR=1 DEBUG_GLSL_PREPROCESSOR=1 HAVE_GLSL_SUPPORT=1"
            BUILD_BYPRODUCTS "${VITAGL_INSTALL_LIB}"
        )

        add_library(vitaGL_imported STATIC IMPORTED GLOBAL)
        set_target_properties(vitaGL_imported PROPERTIES
            IMPORTED_LOCATION "${VITAGL_INSTALL_LIB}")
        add_dependencies(vitaGL_imported vitagl_external)

        message(STATUS "vitaGL: building from source → ${VITAGL_INSTALL_LIB}")
        add_compile_definitions(VITA_HAVE_VITAGL=1)
        target_link_libraries(keeperfx       PRIVATE vitaGL_imported ${VITASHARK_LIB} ${MATHNEON_LIB})
        target_link_libraries(keeperfx_hvlog PRIVATE vitaGL_imported ${VITASHARK_LIB} ${MATHNEON_LIB})
        add_dependencies(keeperfx       vitagl_external)
        add_dependencies(keeperfx_hvlog vitagl_external)
        # Route all malloc/free/calloc/realloc/memalign through vitaGL's allocator
        # so the game heap and GPU memory pool are unified (single pool, no
        # cross-allocator fragmentation).  Uses GCC --wrap at link time so no
        # existing source needs changing.  Technique from d3es-vita.
        target_sources(keeperfx       PRIVATE "src/platform/vita_malloc_wrap.c")
        target_sources(keeperfx_hvlog PRIVATE "src/platform/vita_malloc_wrap.c")
        target_link_options(keeperfx       PRIVATE
            -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc
            -Wl,--wrap,realloc -Wl,--wrap,memalign)
        target_link_options(keeperfx_hvlog PRIVATE
            -Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,calloc
            -Wl,--wrap,realloc -Wl,--wrap,memalign)
        # .gxp shader binaries are pre-compiled manually with tools/psp2cgc.exe and committed.
        # To recompile: tools\psp2cgc.exe -profile sce_vp_psp2 -Wperf -O2 shaders\vita_blit_v.cg -o shaders\vita_blit_v.gxp
        #               tools\psp2cgc.exe -profile sce_fp_psp2 -Wperf -O2 shaders\vita_blit_f.cg -o shaders\vita_blit_f.gxp
        set(VITA_SHADER_VPK_FILES
            "${CMAKE_SOURCE_DIR}/shaders/vita_blit_v.gxp" "shaders/vita_blit_v.gxp"
            "${CMAKE_SOURCE_DIR}/shaders/vita_blit_f.gxp" "shaders/vita_blit_f.gxp")
        add_compile_definitions(VITA_USE_PRECOMPILED_SHADERS=1)
        message(STATUS "vitaGL: using pre-compiled .gxp shaders from shaders/")
        # SceShaccCgExt_stub: vitashark references sceShaccCgExt{Enable,Disable}Extensions at
        # link time.  These are never called when using pre-compiled .gxp shaders, but the
        # linker still requires the symbols.  Build a minimal stub rather than depend on the
        # library being present in the vitasdk container.
        add_library(sceshacccgext_stubs STATIC
            "${CMAKE_SOURCE_DIR}/deps/sceshacccgext-stubs/sceshacccgext_stubs.c")
        target_link_libraries(keeperfx       PRIVATE sceshacccgext_stubs)
        target_link_libraries(keeperfx_hvlog PRIVATE sceshacccgext_stubs)
    else()
        message(STATUS "vitaGL disabled (VITA_ENABLE_VITAGL=OFF) — using SDL2 GXM renderer path.")
        set(VITA_SHADER_VPK_FILES "")
    endif()
    # vitaGL dependency stubs — SceKernelDmacMgr for sceDmacMemcpy (used by vitaGL internally).
    target_link_libraries(keeperfx       PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub ScePower_stub SceAudio_stub SceShaccCg_stub SceKernelDmacMgr_stub SceRazorCapture_stub)
    target_link_libraries(keeperfx_hvlog PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub ScePower_stub SceAudio_stub SceShaccCg_stub SceKernelDmacMgr_stub SceRazorCapture_stub)
    # VITA_SCE_DIAG: compile-in taiHEN import hooks for SCE subsystem tracing.
    # Enabled by the vita-debug preset; compiles to nothing in reldebug/release.
    option(VITA_SCE_DIAG "Enable taiHEN SCE subsystem diagnostic hooks (vita-debug only)" OFF)
    if(VITA_SCE_DIAG)
        target_compile_definitions(keeperfx       PRIVATE VITA_SCE_DIAG=1)
        target_compile_definitions(keeperfx_hvlog PRIVATE VITA_SCE_DIAG=1)
        target_link_libraries(keeperfx       PRIVATE taihen_stub)
        target_link_libraries(keeperfx_hvlog PRIVATE taihen_stub)
        message(STATUS "VITA_SCE_DIAG enabled — taiHEN SCE hooks active in this build")
    endif()
    if(SCESHACCCGEXT_STUB)
        target_link_libraries(keeperfx       PRIVATE ${SCESHACCCGEXT_STUB})
        target_link_libraries(keeperfx_hvlog PRIVATE ${SCESHACCCGEXT_STUB})
    endif()
    # libvorbisfile— floating-point OGG decoder for music playback.
    # Install via: sudo bash -c 'VITASDK=/usr/local/vitasdk /usr/local/vitasdk/bin/vdpm libvorbis libogg'
    find_library(VORBISFILE_LIB vorbisfile HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_library(VORBIS_LIB     vorbis     HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_library(OGG_LIB        ogg        HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    if(VORBISFILE_LIB AND VORBIS_LIB AND OGG_LIB)
        message(STATUS "libvorbisfile found: ${VORBISFILE_LIB}")
        add_compile_definitions(VITA_HAVE_VORBIS=1)
        target_link_libraries(keeperfx       PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
        target_link_libraries(keeperfx_hvlog PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
    else()
        message(STATUS "libvorbisfile not found — OGG music disabled. Install via: sudo bash -c 'VITASDK=/usr/local/vitasdk /usr/local/vitasdk/bin/vdpm libvorbis libogg'")
    endif()
    # FFmpeg-vita (fish47/FFmpeg-vita) — hardware-accelerated H264/AAC/MP3 via SCE APIs.
    # Install: build from source per https://github.com/fish47/FFmpeg-vita
    # FFmpeg has internal circular deps between libavcodec/avutil; --start-group resolves them.
    # pthread is included in the group since avcodec uses it.
    # SceAudiodec_stub: required by FFmpeg-vita's Vita HW audio decoder.
    find_library(AVFORMAT_LIB  avformat    HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(AVCODEC_LIB   avcodec     HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(AVUTIL_LIB    avutil      HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(SWRESAMPLE_LIB swresample HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(MP3LAME_LIB   mp3lame    HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    message(STATUS "FFmpeg-vita found: ${AVFORMAT_LIB}")
    # ffmpeg_vita_stubs: no-op stubs for symbols absent from fish47/FFmpeg-vita build.
    # Built from source (deps/ffmpeg-vita-stubs/) so CI doesn't need a pre-installed lib.
    # Currently provides: ff_ac3_find_syncword (returning -1 disables AC3 FMV audio).
    add_library(ffmpeg_vita_stubs STATIC deps/ffmpeg-vita-stubs/ffmpeg_vita_stubs.c)
    target_link_libraries(keeperfx       PRIVATE -Wl,--start-group ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${MP3LAME_LIB} pthread SceAudiodec_stub ffmpeg_vita_stubs -Wl,--end-group)
    target_link_libraries(keeperfx_hvlog PRIVATE -Wl,--start-group ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${MP3LAME_LIB} pthread SceAudiodec_stub ffmpeg_vita_stubs -Wl,--end-group)
    target_link_libraries(keeperfx PRIVATE centijson_static centitoml)
    target_link_libraries(keeperfx_hvlog PRIVATE centijson_static centitoml)
    # zlib, spng, minizip — built from source for Vita (not in vitasdk sysroot).
    # custom_sprites.c uses spng for PNG decoding and minizip for ZIP reading from mod packs.
    include(FetchContent)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)  # allow FetchContent_Populate
    endif()
    FetchContent_Declare(zlib_src
        URL      https://github.com/madler/zlib/archive/refs/tags/v1.3.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(zlib_src)
    if(NOT zlib_src_POPULATED)
        FetchContent_Populate(zlib_src)
    endif()
    add_library(vita_zlib STATIC
        ${zlib_src_SOURCE_DIR}/adler32.c
        ${zlib_src_SOURCE_DIR}/compress.c
        ${zlib_src_SOURCE_DIR}/crc32.c
        ${zlib_src_SOURCE_DIR}/deflate.c
        ${zlib_src_SOURCE_DIR}/gzclose.c
        ${zlib_src_SOURCE_DIR}/gzlib.c
        ${zlib_src_SOURCE_DIR}/gzread.c
        ${zlib_src_SOURCE_DIR}/gzwrite.c
        ${zlib_src_SOURCE_DIR}/infback.c
        ${zlib_src_SOURCE_DIR}/inffast.c
        ${zlib_src_SOURCE_DIR}/inflate.c
        ${zlib_src_SOURCE_DIR}/inftrees.c
        ${zlib_src_SOURCE_DIR}/trees.c
        ${zlib_src_SOURCE_DIR}/uncompr.c
        ${zlib_src_SOURCE_DIR}/zutil.c)
    target_include_directories(vita_zlib PUBLIC ${zlib_src_SOURCE_DIR})
    # minizip from zlib contrib (read-only ZIP access for custom sprite mod packs)
    add_library(vita_minizip STATIC
        ${zlib_src_SOURCE_DIR}/contrib/minizip/unzip.c
        ${zlib_src_SOURCE_DIR}/contrib/minizip/ioapi.c)
    target_include_directories(vita_minizip PUBLIC ${zlib_src_SOURCE_DIR}/contrib)
    target_compile_definitions(vita_minizip PRIVATE USE_FILE32API=1)  # vitasdk newlib lacks fopen64/ftell64
    target_link_libraries(vita_minizip PUBLIC vita_zlib)
    FetchContent_Declare(spng_src
        URL      https://github.com/randy408/libspng/archive/refs/tags/v0.7.4.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(spng_src)
    if(NOT spng_src_POPULATED)
        FetchContent_Populate(spng_src)
    endif()
    add_library(vita_spng STATIC ${spng_src_SOURCE_DIR}/spng/spng.c)
    target_include_directories(vita_spng PUBLIC ${spng_src_SOURCE_DIR}/spng)
    target_compile_definitions(vita_spng PUBLIC SPNG_STATIC=1)
    target_link_libraries(vita_spng PUBLIC vita_zlib)
    # custom_sprites.c needs spng.h and minizip/unzip.h
    target_include_directories(keeperfx       PRIVATE ${spng_src_SOURCE_DIR}/spng ${zlib_src_SOURCE_DIR}/contrib)
    target_include_directories(keeperfx_hvlog PRIVATE ${spng_src_SOURCE_DIR}/spng ${zlib_src_SOURCE_DIR}/contrib)
    target_link_libraries(keeperfx       PRIVATE vita_spng vita_minizip)
    target_link_libraries(keeperfx_hvlog PRIVATE vita_spng vita_minizip)
    # LuaJIT for the Vita — cross-compiled via SonicMastr/LuaJIT-Vita fork.
    # Install prerequisites: vdpm luajit taihen vita-rss-libdl
    #
    # WHY taihen is required at runtime:
    #   LuaJIT's JIT compiler must allocate read-write-execute memory pages.  The
    #   Vita OS normally disallows user code from creating executable mappings.
    #   taiHEN is a kernel plugin (part of HENkaku / any homebrew-enabled Vita) that
    #   patches those restrictions.  Any Vita running homebrew already has taiHEN
    #   loaded (it is a prerequisite of every homebrew launcher), so end users do not
    #   need to install anything extra.  The taihen_stub.a at link time simply
    #   generates the SVC stubs to call into the kernel plugin at runtime.
    #   vita-rss-libdl provides the dlopen/dlsym shim that LuaJIT uses for C FFI.
    find_library(LUAJIT_LIB luajit-5.1 HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_path(LUAJIT_INCLUDE luajit.h HINTS "$ENV{VITASDK}/arm-vita-eabi/include/luajit-2.1" REQUIRED)
    message(STATUS "LuaJIT found: ${LUAJIT_LIB}")
    target_include_directories(keeperfx PRIVATE ${LUAJIT_INCLUDE})
    target_include_directories(keeperfx_hvlog PRIVATE ${LUAJIT_INCLUDE})
    target_link_libraries(keeperfx PRIVATE -Wl,--start-group ${LUAJIT_LIB} taihen_stub taihenModuleUtils_stub SceSblSsMgr_stub dl -Wl,--end-group)
    target_link_libraries(keeperfx_hvlog PRIVATE -Wl,--start-group ${LUAJIT_LIB} taihen_stub taihenModuleUtils_stub SceSblSsMgr_stub dl -Wl,--end-group)

    # VPK packaging — built automatically via build preset targets
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    set(VITA_APP_NAME "KeeperFX")
    set(VITA_TITLEID  "KFXV00001")
    set(VITA_VERSION  "01.00")
    # UNSAFE needed because LuaJIT requires taiHEN RWX memory (standard on HENkaku consoles)
    vita_create_self(keeperfx.self keeperfx UNSAFE)
    vita_create_vpk(keeperfx.vpk ${VITA_TITLEID} keeperfx.self
        VERSION ${VITA_VERSION}
        NAME    ${VITA_APP_NAME}
        FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/icon0.png                          sce_sys/icon0.png
        FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/bg.png           sce_sys/livearea/contents/bg.png
        FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/startup.png      sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/template.xml     sce_sys/livearea/contents/template.xml
        ${VITA_SHADER_VPK_FILES}
    )
elseif(PLATFORM_3DS)
    target_link_libraries(keeperfx PRIVATE citro3d ctru)
    target_link_libraries(keeperfx_hvlog PRIVATE citro3d ctru)
elseif(PLATFORM_SWITCH)
    target_link_libraries(keeperfx PRIVATE nx)
    target_link_libraries(keeperfx_hvlog PRIVATE nx)
else()
    # When cross-compiling for Windows (MinGW), always use static SDL2 targets to
    # produce a self-contained exe that doesn't need SDL2*.dll at runtime.
    if(MINGW OR CMAKE_CROSSCOMPILING)
        set(_SDL2_LINK SDL2::SDL2-static)
        set(_SDL2_IMAGE_LINK SDL2_image::SDL2_image-static)
        set(_SDL2_MIXER_LINK SDL2_mixer::SDL2_mixer-static)
        if(KEEPERFX_NETWORKING)
            set(_SDL2_NET_LINK SDL2_net::SDL2_net-static)
        endif()
    else()
        set(_SDL2_LINK $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
        set(_SDL2_IMAGE_LINK $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,SDL2_image::SDL2_image-static>)
        set(_SDL2_MIXER_LINK $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>)
        if(KEEPERFX_NETWORKING)
            set(_SDL2_NET_LINK $<IF:$<TARGET_EXISTS:SDL2_net::SDL2_net>,SDL2_net::SDL2_net,SDL2_net::SDL2_net-static>)
        endif()
    endif()

    foreach(_kfx_target keeperfx keeperfx_hvlog tests)
        target_link_libraries(${_kfx_target} PRIVATE
            ${_SDL2_LINK}
            ${_SDL2_IMAGE_LINK}
            ${_SDL2_MIXER_LINK})
        if(KEEPERFX_NETWORKING)
            target_link_libraries(${_kfx_target} PRIVATE ${_SDL2_NET_LINK})
        endif()
    endforeach()
endif()

# OpenGL renderer backend
if(KEEPERFX_RENDERER_OPENGL)
    foreach(target keeperfx keeperfx_hvlog tests)
        target_compile_definitions(${target} PRIVATE RENDERER_OPENGL_ENABLED=1)
        target_link_libraries(${target} PRIVATE glad::glad opengl32)
    endforeach()
endif()
