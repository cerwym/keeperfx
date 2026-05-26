cmake_minimum_required(VERSION 3.20)

# -------------------------------------------------------------------
# Library linking — all dependencies provided by vcpkg.
#
# The custom registry (cerwym/keeperfx-vcpkg-registry) provides:
#   enet6, centijson, astronomy, libnatpmp
#
# All other packages come from the official vcpkg registry.
#
# Platform differences (triplet, compiler, linkage) are handled
# transparently by vcpkg — no per-platform branching needed here.
# -------------------------------------------------------------------

## enet6
find_package(enet6 CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC enet6::enet6)
target_link_libraries(keeperfx_hvlog PUBLIC enet6::enet6)

## spng (libspng)
find_package(SPNG CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)
target_link_libraries(keeperfx_hvlog PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)

## centijson
find_package(centijson CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC centijson::centijson)
target_link_libraries(keeperfx_hvlog PUBLIC centijson::centijson)

## astronomy
find_package(astronomy CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC astronomy::astronomy)
target_link_libraries(keeperfx_hvlog PUBLIC astronomy::astronomy)

## zlib
find_package(ZLIB REQUIRED)
target_link_libraries(keeperfx PUBLIC ZLIB::ZLIB)
target_link_libraries(keeperfx_hvlog PUBLIC ZLIB::ZLIB)

## minizip
find_package(unofficial-minizip CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC unofficial::minizip::minizip)
target_link_libraries(keeperfx_hvlog PUBLIC unofficial::minizip::minizip)

## centitoml (compiled from bundled source; links against centijson from vcpkg)
add_library(centitoml OBJECT "centitoml/toml_api.c")
target_link_libraries(centitoml PUBLIC centijson::centijson)
target_include_directories(centitoml INTERFACE "centitoml")
target_link_libraries(keeperfx PUBLIC centitoml)
target_link_libraries(keeperfx_hvlog PUBLIC centitoml)

## ffmpeg
find_package(FFMPEG COMPONENTS AVCODEC AVFORMAT AVUTIL SWRESAMPLE REQUIRED)
target_include_directories(keeperfx PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_include_directories(keeperfx_hvlog PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_link_directories(keeperfx PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_directories(keeperfx_hvlog PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(keeperfx PUBLIC ${FFMPEG_LIBRARIES})
target_link_libraries(keeperfx_hvlog PUBLIC ${FFMPEG_LIBRARIES})
if(WIN32)
    target_link_libraries(keeperfx PUBLIC bcrypt)
    target_link_libraries(keeperfx_hvlog PUBLIC bcrypt)
endif()

## luajit
find_library(LUAJIT_LIB NAMES lua51 luajit-5.1 luajit REQUIRED)
find_path(LUAJIT_INCLUDE NAMES lua.h PATH_SUFFIXES luajit luajit-5.1 REQUIRED)
target_link_libraries(keeperfx PUBLIC ${LUAJIT_LIB})
target_link_libraries(keeperfx_hvlog PUBLIC ${LUAJIT_LIB})
target_include_directories(keeperfx PUBLIC ${LUAJIT_INCLUDE})
target_include_directories(keeperfx_hvlog PUBLIC ${LUAJIT_INCLUDE})

## openal
find_package(OpenAL CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC OpenAL::OpenAL)
target_link_libraries(keeperfx_hvlog PUBLIC OpenAL::OpenAL)

## miniupnpc
find_package(miniupnpc CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC miniupnpc::miniupnpc)
target_link_libraries(keeperfx_hvlog PUBLIC miniupnpc::miniupnpc)

## libnatpmp
find_package(libnatpmp CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC libnatpmp::libnatpmp)
target_link_libraries(keeperfx_hvlog PUBLIC libnatpmp::libnatpmp)

## curl (matchmaking WebSocket client) — enabled by Dependencies.cmake detection
if(KEEPERFX_MATCHMAKING)
    find_package(CURL CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC CURL::libcurl)
    target_link_libraries(keeperfx_hvlog PUBLIC CURL::libcurl)
endif()

