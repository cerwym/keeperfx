cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Dependency downloader — all deps sourced from dkfans/kfx-deps releases.
#
# KFX_DEPS_CACHE: directory with pre-downloaded tarballs (e.g. in the Docker
# image at /deps-cache).  Set via -DKFX_DEPS_CACHE=<path> or the env var.
# ---------------------------------------------------------------------------

if(NOT KFX_DEPS_CACHE)
    set(KFX_DEPS_CACHE "$ENV{KFX_DEPS_CACHE}")
endif()

# Canonical kfx-deps release tag.  All deps come from this single release.
set(KFX_DEPS_RELEASE "20260213")
set(KFX_DEPS_BASE "https://github.com/dkfans/kfx-deps/releases/download/${KFX_DEPS_RELEASE}")

# Select arch suffix based on pointer size (matches mingw32 vs mingw64 toolchain)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(KFX_DEPS_ARCH "mingw32")
else()
    set(KFX_DEPS_ARCH "mingw64")
endif()

macro(kfx_fetch_tarball TARBALL URL)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/deps/${TARBALL}")
        if(KFX_DEPS_CACHE AND EXISTS "${KFX_DEPS_CACHE}/${TARBALL}")
            message(STATUS "kfx-deps: copying ${TARBALL} from cache ${KFX_DEPS_CACHE}")
            file(COPY "${KFX_DEPS_CACHE}/${TARBALL}" DESTINATION "${CMAKE_SOURCE_DIR}/deps/")
        else()
            message(STATUS "kfx-deps: downloading ${TARBALL}")
            file(DOWNLOAD "${URL}" "${CMAKE_SOURCE_DIR}/deps/${TARBALL}" SHOW_PROGRESS)
        endif()
    endif()
endmacro()

macro(kfx_fetch_dep DEP_NAME)
    set(_tarball "${DEP_NAME}-${KFX_DEPS_ARCH}.tar.gz")
    set(_dir "${CMAKE_SOURCE_DIR}/deps/${DEP_NAME}")
    if(NOT EXISTS "${_dir}")
        file(MAKE_DIRECTORY "${_dir}")
        kfx_fetch_tarball("${_tarball}" "${KFX_DEPS_BASE}/${_tarball}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${CMAKE_SOURCE_DIR}/deps/${_tarball}" WORKING_DIRECTORY "${_dir}")
    endif()
endmacro()

kfx_fetch_dep(enet6)
kfx_fetch_dep(zlib)
kfx_fetch_dep(spng)
kfx_fetch_dep(astronomy)
kfx_fetch_dep(centijson)
kfx_fetch_dep(ffmpeg)
kfx_fetch_dep(openal)
kfx_fetch_dep(luajit)
kfx_fetch_dep(miniupnpc)
kfx_fetch_dep(libnatpmp)

## enet6
add_library(enet_static STATIC IMPORTED)
set_target_properties(enet_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/enet6/lib/libenet6.a)
set_target_properties(enet_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/enet6/include)
target_link_libraries(enet_static INTERFACE ws2_32 winmm)
target_link_libraries(keeperfx PUBLIC enet_static)
target_link_libraries(keeperfx_hvlog PUBLIC enet_static)
target_link_libraries(tests PUBLIC enet_static)

## spng
add_library(spng_static STATIC IMPORTED)
set_target_properties(spng_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/spng/libspng.a)
set_target_properties(spng_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/spng/include)
target_link_libraries(keeperfx PUBLIC spng_static)
target_link_libraries(keeperfx_hvlog PUBLIC spng_static)
target_link_libraries(tests PUBLIC spng_static)

## centijson
add_library(centijson_static STATIC IMPORTED)
set_target_properties(centijson_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/centijson/libjson.a)
set_target_properties(centijson_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/centijson/include)
target_link_libraries(keeperfx PUBLIC centijson_static)
target_link_libraries(keeperfx_hvlog PUBLIC centijson_static)
target_link_libraries(tests PUBLIC centijson_static)

## astronomy.
add_library(astronomy_static STATIC IMPORTED)
set_target_properties(astronomy_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/astronomy/libastronomy.a)
set_target_properties(astronomy_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/astronomy/include)
target_link_libraries(keeperfx PUBLIC astronomy_static)
target_link_libraries(keeperfx_hvlog PUBLIC astronomy_static)
target_link_libraries(tests PUBLIC astronomy_static)

## zlib
add_library(zlib_static STATIC IMPORTED)
set_target_properties(zlib_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/zlib/libz.a)
set_target_properties(zlib_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include)
target_link_libraries(keeperfx PUBLIC zlib_static)
target_link_libraries(keeperfx_hvlog PUBLIC zlib_static)
target_link_libraries(tests PUBLIC zlib_static)

# Add minizip
add_library(minizip_static STATIC IMPORTED)
set_target_properties(minizip_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/zlib/libminizip.a)
set_target_properties(minizip_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include)
target_link_libraries(minizip_static INTERFACE zlib_static)
target_link_libraries(keeperfx PUBLIC minizip_static)
target_link_libraries(keeperfx_hvlog PUBLIC minizip_static)
target_link_libraries(tests PUBLIC minizip_static)

## centitoml.
add_library(centitoml OBJECT "centitoml/toml_api.c")
target_link_libraries(centitoml PUBLIC centijson_static)
target_include_directories(centitoml INTERFACE "centitoml")
target_link_libraries(keeperfx PUBLIC centitoml)
target_link_libraries(keeperfx_hvlog PUBLIC centitoml)
target_link_libraries(tests PUBLIC centitoml)

## ffmpeg
add_library(libavcodec_static STATIC IMPORTED)
set_target_properties(libavcodec_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/libavcodec/libavcodec.a)
set_target_properties(libavcodec_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)

add_library(libavformat_static STATIC IMPORTED)
set_target_properties(libavformat_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/libavformat/libavformat.a)
set_target_properties(libavformat_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)

add_library(libavutil_static STATIC IMPORTED)
set_target_properties(libavutil_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/libavutil/libavutil.a)
set_target_properties(libavutil_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)

add_library(libswresample_static STATIC IMPORTED)
set_target_properties(libswresample_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/libswresample/libswresample.a)
set_target_properties(libswresample_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg)

target_link_libraries(keeperfx PUBLIC -Wl,--start-group libavformat_static libavcodec_static libswresample_static libavutil_static bcrypt -Wl,--end-group)
target_link_libraries(keeperfx_hvlog PUBLIC -Wl,--start-group libavformat_static libavcodec_static libswresample_static libavutil_static bcrypt -Wl,--end-group)
target_link_libraries(tests PUBLIC -Wl,--start-group libavformat_static libavcodec_static libswresample_static libavutil_static bcrypt -Wl,--end-group)

## OpenAL
add_library(openal_static STATIC IMPORTED)
set_target_properties(openal_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/openal/libOpenAL32.a)
set_target_properties(openal_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/openal/include)
# libOpenAL32.a is a static archive — tell headers not to use __declspec(dllimport)
target_compile_definitions(openal_static INTERFACE AL_LIBTYPE_STATIC=1)

# libOpenAL32.a was built with GCC win32 threading; our posix-threading toolchain
# doesn't provide __gthr_win32_* symbols. Provide shims via a small helper object.
add_library(openal_gthr_shims STATIC ${CMAKE_CURRENT_SOURCE_DIR}/openal_gthr_shims.c)

target_link_libraries(keeperfx PUBLIC openal_static openal_gthr_shims)
target_link_libraries(keeperfx_hvlog PUBLIC openal_static openal_gthr_shims)
target_link_libraries(tests PUBLIC openal_static openal_gthr_shims)

## LuaJIT
add_library(luajit_static STATIC IMPORTED)
set_target_properties(luajit_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/luajit/lib/libluajit.a)
set_target_properties(luajit_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/luajit/include)
target_link_libraries(keeperfx PUBLIC luajit_static)
target_link_libraries(keeperfx_hvlog PUBLIC luajit_static)
target_link_libraries(tests PUBLIC luajit_static)

## miniupnpc
add_library(miniupnpc_static STATIC IMPORTED)
set_target_properties(miniupnpc_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/miniupnpc/libminiupnpc.a)
set_target_properties(miniupnpc_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/miniupnpc/include)
target_link_libraries(keeperfx PUBLIC miniupnpc_static)
target_link_libraries(keeperfx_hvlog PUBLIC miniupnpc_static)
target_link_libraries(tests PUBLIC miniupnpc_static)

## libnatpmp
add_library(libnatpmp_static STATIC IMPORTED)
set_target_properties(libnatpmp_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libnatpmp/libnatpmp.a)
set_target_properties(libnatpmp_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/libnatpmp/include)
target_link_libraries(libnatpmp_static INTERFACE iphlpapi)
target_link_libraries(keeperfx PUBLIC libnatpmp_static)
target_link_libraries(keeperfx_hvlog PUBLIC libnatpmp_static)
target_link_libraries(tests PUBLIC libnatpmp_static)
