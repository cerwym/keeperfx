cmake_minimum_required(VERSION 3.20)

# Do not allow building in root
if (CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(FATAL_ERROR "Do not build in the root. Create a bin directory and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif ()

project(keeperfx C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Version information
set(VER_MAJOR       1)
set(VER_MINOR       3)
set(VER_RELEASE     1)
set(VER_BUILD       0)
set(VER_STRING      "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${VER_BUILD} ${PACKAGE_SUFFIX}")
set(PACKAGE_SUFFIX  "")

# Generate version header
set(KEEPERFX_VER_DEFS_H_IN ${CMAKE_SOURCE_DIR}/build/ver_defs.h.in)
set(KEEPERFX_VER_DEFS_H_OUT ${CMAKE_SOURCE_DIR}/src/ver_defs.h)
configure_file(${KEEPERFX_VER_DEFS_H_IN} ${KEEPERFX_VER_DEFS_H_OUT})

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Load CMake Modules
# See: build/cmake/modules/
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/build/cmake/modules)

include(Helpers)
include(Platforms)
include(Dependencies)
include(BuildTargets)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Subdirectories (desktop only)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Go into submodules (desktop only — homebrew platforms use their own SDK deps).
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH AND NOT PLATFORM_WII_U)
    add_subdirectory(deps)
    add_subdirectory(tools)
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Vita-specific setup (PlayStation Vita support via vitaGL + FFmpeg-vita)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(PLATFORM_VITA)
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    include(ExternalProject)
    
    # SDL2 from vitasdk
    target_link_libraries(keeperfx PRIVATE SDL2::SDL2)
    target_link_libraries(keeperfx_hvlog PRIVATE SDL2::SDL2)
    
    # Cortex-A9 NEON optimization
    target_compile_options(keeperfx       PRIVATE -mcpu=cortex-a9 -mfpu=neon -ffast-math)
    target_compile_options(keeperfx_hvlog PRIVATE -mcpu=cortex-a9 -mfpu=neon -ffast-math)
    
    # DWARF-4 debug info for crash analysis
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(keeperfx       PRIVATE -gdwarf-4)
        target_compile_options(keeperfx_hvlog PRIVATE -gdwarf-4)
    endif()
    
    # vitaGL: build from source (requires vitashark + mathneon via vdpm)
    find_library(VITASHARK_LIB vitashark HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_library(MATHNEON_LIB  mathneon  HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    if(NOT VITASHARK_LIB OR NOT MATHNEON_LIB)
        message(FATAL_ERROR "vitashark/mathneon not found.\nInstall via:  $VITASDK/bin/vdpm vitashark mathneon")
    endif()
    
    set(VITAGL_INSTALL_LIB "$ENV{VITASDK}/arm-vita-eabi/lib/libvitaGL.a")
    ExternalProject_Add(vitagl_external
        GIT_REPOSITORY  https://github.com/Rinnegatamante/vitaGL.git
        GIT_TAG         e6011789a48be7f483b840e5e276f50253c06604
        GIT_SHALLOW     FALSE
        SOURCE_DIR      ${CMAKE_BINARY_DIR}/_deps/vitagl-src
        BUILD_IN_SOURCE TRUE
        CONFIGURE_COMMAND ""
        PATCH_COMMAND   git reset --hard HEAD && git clean -fd
        BUILD_COMMAND   /bin/bash -c "rm -f $ENV{VITASDK}/arm-vita-eabi/lib/libvitaGL.a $ENV{VITASDK}/arm-vita-eabi/include/vitaGL.h $ENV{VITASDK}/arm-vita-eabi/include/imgui_impl_vitagl.h && make LOG_ERRORS=2 HAVE_RAZOR=0 HAVE_PROFILING=0 HAVE_DEBUGGER=1 DEBUG_GLSL_TRANSLATOR=1 DEBUG_GLSL_PREPROCESSOR=1 HAVE_GLSL_SUPPORT=1"
        INSTALL_COMMAND /bin/bash -c "make install LOG_ERRORS=2 HAVE_RAZOR=0 HAVE_PROFILING=0 HAVE_DEBUGGER=1 DEBUG_GLSL_TRANSLATOR=1 DEBUG_GLSL_PREPROCESSOR=1 HAVE_GLSL_SUPPORT=1"
        BUILD_BYPRODUCTS "${VITAGL_INSTALL_LIB}")
    
    add_library(vitaGL_imported STATIC IMPORTED GLOBAL)
    set_target_properties(vitaGL_imported PROPERTIES IMPORTED_LOCATION "${VITAGL_INSTALL_LIB}")
    add_dependencies(vitaGL_imported vitagl_external)
    
    target_link_libraries(keeperfx       PRIVATE vitaGL_imported ${VITASHARK_LIB} ${MATHNEON_LIB})
    target_link_libraries(keeperfx_hvlog PRIVATE vitaGL_imported ${VITASHARK_LIB} ${MATHNEON_LIB})
    add_dependencies(keeperfx       vitagl_external)
    add_dependencies(keeperfx_hvlog vitagl_external)
    
    target_sources(keeperfx       PRIVATE "src/platform/stack_monitor.c")
    target_sources(keeperfx_hvlog PRIVATE "src/platform/stack_monitor.c")
    
    # Vita stubs and support libraries
    add_library(sceshacccgext_stubs STATIC "${CMAKE_SOURCE_DIR}/deps/sceshacccgext-stubs/sceshacccgext_stubs.c")
    target_link_libraries(keeperfx       PRIVATE sceshacccgext_stubs)
    target_link_libraries(keeperfx_hvlog PRIVATE sceshacccgext_stubs)
    
    target_link_libraries(keeperfx       PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub ScePower_stub SceAudio_stub SceShaccCg_stub SceKernelDmacMgr_stub)
    target_link_libraries(keeperfx_hvlog PRIVATE SceGxm_stub SceDisplay_stub SceCtrl_stub SceTouch_stub ScePower_stub SceAudio_stub SceShaccCg_stub SceKernelDmacMgr_stub)
    
    if(VITA_SCE_DIAG)
        target_link_libraries(keeperfx       PRIVATE taihen_stub)
        target_link_libraries(keeperfx_hvlog PRIVATE taihen_stub)
    endif()
    
    # Optional: libvorbisfile for OGG audio
    find_library(VORBISFILE_LIB vorbisfile HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_library(VORBIS_LIB vorbis HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_library(OGG_LIB ogg HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    if(VORBISFILE_LIB AND VORBIS_LIB AND OGG_LIB)
        add_compile_definitions(VITA_HAVE_VORBIS=1)
        target_link_libraries(keeperfx       PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
        target_link_libraries(keeperfx_hvlog PRIVATE ${VORBISFILE_LIB} ${VORBIS_LIB} ${OGG_LIB})
    endif()
    
    # FFmpeg-vita (hardware-accelerated video/audio)
    find_library(AVFORMAT_LIB  avformat    HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(AVCODEC_LIB   avcodec     HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(AVUTIL_LIB    avutil      HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(SWRESAMPLE_LIB swresample HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    find_library(MP3LAME_LIB   mp3lame    HINTS "$ENV{VITASDK}/arm-vita-eabi/lib" REQUIRED)
    
    add_library(ffmpeg_vita_stubs STATIC deps/ffmpeg-vita-stubs/ffmpeg_vita_stubs.c)
    target_link_libraries(keeperfx       PRIVATE -Wl,--start-group ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${MP3LAME_LIB} pthread SceAudiodec_stub ffmpeg_vita_stubs -Wl,--end-group)
    target_link_libraries(keeperfx_hvlog PRIVATE -Wl,--start-group ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${MP3LAME_LIB} pthread SceAudiodec_stub ffmpeg_vita_stubs -Wl,--end-group)
    
    target_link_libraries(keeperfx PRIVATE centijson_static centitoml)
    target_link_libraries(keeperfx_hvlog PRIVATE centijson_static centitoml)
    
    # zlib + spng + minizip (for custom sprite support)
    include(FetchContent)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
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
    
    add_library(vita_minizip STATIC
        ${zlib_src_SOURCE_DIR}/contrib/minizip/unzip.c
        ${zlib_src_SOURCE_DIR}/contrib/minizip/ioapi.c)
    target_include_directories(vita_minizip PUBLIC ${zlib_src_SOURCE_DIR}/contrib)
    target_compile_definitions(vita_minizip PRIVATE USE_FILE32API=1)
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
    
    target_include_directories(keeperfx       PRIVATE ${spng_src_SOURCE_DIR}/spng ${zlib_src_SOURCE_DIR}/contrib)
    target_include_directories(keeperfx_hvlog PRIVATE ${spng_src_SOURCE_DIR}/spng ${zlib_src_SOURCE_DIR}/contrib)
    target_link_libraries(keeperfx       PRIVATE vita_spng vita_minizip vita_zlib)
    target_link_libraries(keeperfx_hvlog PRIVATE vita_spng vita_minizip vita_zlib)
    
    # Lua (SonicMastr/LuaJIT-Vita via vdpm)
    find_library(LUAJIT_LIB luajit-5.1 HINTS "$ENV{VITASDK}/arm-vita-eabi/lib")
    find_path(LUAJIT_INCLUDE_DIR lua.h
        HINTS
            "$ENV{VITASDK}/arm-vita-eabi/include/luajit-2.1"
            "$ENV{VITASDK}/arm-vita-eabi/include/luajit-2.0"
            "$ENV{VITASDK}/arm-vita-eabi/include")
    if(LUAJIT_LIB AND LUAJIT_INCLUDE_DIR)
        target_include_directories(keeperfx       PRIVATE ${LUAJIT_INCLUDE_DIR})
        target_include_directories(keeperfx_hvlog PRIVATE ${LUAJIT_INCLUDE_DIR})
        # libdl provides POSIX dlopen/dlsym/dlclose/dlerror shims required by
        # LuaJIT's lj_clib and lib_package objects on the Vita (no native libdl).
        # libdl itself depends on taihen (taiGetModuleInfo) and SceSblSsMgr (sceSblDmac5HashTransform).
        target_link_libraries(keeperfx       PRIVATE ${LUAJIT_LIB} dl taihen_stub SceSblSsMgr_stub)
        target_link_libraries(keeperfx_hvlog PRIVATE ${LUAJIT_LIB} dl taihen_stub SceSblSsMgr_stub)
    elseif(LUAJIT_LIB AND NOT LUAJIT_INCLUDE_DIR)
        message(FATAL_ERROR "LuaJIT library found but lua.h not found.\nInstall LuaJIT headers via: \$VITASDK/bin/vdpm luajit")
    endif()

    # ━━━ Vita package (.self + .vpk) ━━━
    # vita_create_self / vita_create_vpk are provided by vitasdk's vita.toolchain.cmake.
    # UNSAFE is required for vitaGL homebrew which uses unrestricted memory.
    vita_create_self(keeperfx.self keeperfx UNSAFE)
    vita_create_vpk(keeperfx.vpk KFXV00001 keeperfx.self
        VERSION "01.00"
        NAME    "KeeperFX"
        FILE    ${CMAKE_SOURCE_DIR}/vita/sce_sys/icon0.png                             sce_sys/icon0.png
        FILE    ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/bg.png              sce_sys/livearea/contents/bg.png
        FILE    ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/startup.png         sce_sys/livearea/contents/startup.png
        FILE    ${CMAKE_SOURCE_DIR}/vita/sce_sys/livearea/contents/template.xml        sce_sys/livearea/contents/template.xml
    )
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 3DS-specific setup (Nintendo 3DS support via citro3d + devkitARM)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(PLATFORM_3DS)
    # devkitARM and 3DS SDK paths from DEVKITARM environment variable
    if(NOT DEVKITARM)
        message(FATAL_ERROR "DEVKITARM environment variable not set. Install devkitARM from devkitPro")
    endif()
    
    # Optimization flags (architecture-specific flags are set by the toolchain)
    target_compile_options(keeperfx       PRIVATE -ffast-math)
    target_compile_options(keeperfx_hvlog PRIVATE -ffast-math)
    
    # citro3d library (PICA200 hardware abstraction)
    find_library(CITRO3D_LIB citro3d HINTS "${DEVKITPRO}/libctru/lib" REQUIRED)
    find_library(CTR_LIB ctru HINTS "${DEVKITPRO}/libctru/lib" REQUIRED)
    find_library(M_LIB m HINTS "${DEVKITARM}/lib")
    
    # 3D graphics via citro3d
    target_link_libraries(keeperfx       PRIVATE ${CITRO3D_LIB} ${CTR_LIB} ${M_LIB})
    target_link_libraries(keeperfx_hvlog PRIVATE ${CITRO3D_LIB} ${CTR_LIB} ${M_LIB})
    
    # Debug symbols for crash analysis
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(keeperfx       PRIVATE -g)
        target_compile_options(keeperfx_hvlog PRIVATE -g)
    endif()
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Switch-specific setup (Nintendo Switch support via libnx)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(PLATFORM_SWITCH)
    # devkitA64 and Switch SDK paths from DEVKITA64 environment variable or infer from DEVKITPRO
    if(NOT DEVKITA64)
        if(DEVKITPRO)
            set(DEVKITA64 "${DEVKITPRO}/devkitA64")
            message(STATUS "DEVKITA64 inferred from DEVKITPRO: ${DEVKITA64}")
        else()
            message(FATAL_ERROR "DEVKITA64 environment variable not set and DEVKITPRO not found. Install devkitA64 from devkitPro")
        endif()
    endif()

    include_directories($ENV{DEVKITPRO}/portlibs/switch/include)
    set(USE_SYSTEM_SDL2_DEFAULT ON)
    
    # Optimization flags (architecture-specific flags are set by the toolchain)
    target_compile_options(keeperfx       PRIVATE -ffast-math)
    target_compile_options(keeperfx_hvlog PRIVATE -ffast-math)
    
    # libnx core library
    find_library(NX_LIB nx HINTS "${DEVKITA64}/lib" REQUIRED)
    find_library(M_LIB m HINTS "${DEVKITA64}/lib")
    
    # Audio rendering (audren service)v
    find_library(AUDREN_LIB audren HINTS "${DEVKITA64}/lib")
    if(AUDREN_LIB)
        target_link_libraries(keeperfx       PRIVATE ${AUDREN_LIB})
        target_link_libraries(keeperfx_hvlog PRIVATE ${AUDREN_LIB})
    endif()
    
    # Core libnx linkage
    target_link_libraries(keeperfx       PRIVATE ${NX_LIB} ${M_LIB})
    target_link_libraries(keeperfx_hvlog PRIVATE ${NX_LIB} ${M_LIB})
    
    # Debug symbols for crash analysis
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(keeperfx       PRIVATE -g)
        target_compile_options(keeperfx_hvlog PRIVATE -g)
    endif()
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Wii U-specific setup (Nintendo Wii U support via libgfd + libgx2)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(PLATFORM_WII_U)
    # devkitPPC and Wii U SDK paths from DEVKITPPC environment variable
    if(NOT DEVKITPPC)
        message(FATAL_ERROR "DEVKITPPC environment variable not set. Install devkitPPC from devkitPro")
    endif()
    
    # Optimization flags (architecture-specific flags are set by the toolchain)
    target_compile_options(keeperfx       PRIVATE -ffast-math)
    target_compile_options(keeperfx_hvlog PRIVATE -ffast-math)
    
    # Graphics library (GX2)
    find_library(GX2_LIB gx2 HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    
    # Graphics format library
    find_library(GFD_LIB gfd HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    
    # Audio subsystem
    find_library(AX_LIB ax HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    
    # Core Wii U library
    find_library(WIIU_LIB wiiu HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    find_library(M_LIB m HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    
    # Memory and system libraries
    find_library(COREINIT_LIB coreinit HINTS "${DEVKITPRO}/libwut/lib" "${DEVKITPPC}/lib")
    
    # Link all graphics and audio subsystems
    target_link_libraries(keeperfx       PRIVATE ${GX2_LIB} ${GFD_LIB} ${AX_LIB} ${WIIU_LIB} ${COREINIT_LIB} ${M_LIB})
    target_link_libraries(keeperfx_hvlog PRIVATE ${GX2_LIB} ${GFD_LIB} ${AX_LIB} ${WIIU_LIB} ${COREINIT_LIB} ${M_LIB})
    
    # Debug symbols for crash analysis
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(keeperfx       PRIVATE -g)
        target_compile_options(keeperfx_hvlog PRIVATE -g)
    endif()
endif()
