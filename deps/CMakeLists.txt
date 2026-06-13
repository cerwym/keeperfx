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

## spng (libspng)
find_package(SPNG CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)

## centijson
find_package(centijson CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC centijson::centijson)

## astronomy
find_package(astronomy CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC astronomy::astronomy)

## zlib
find_package(ZLIB REQUIRED)
target_link_libraries(keeperfx PUBLIC ZLIB::ZLIB)

## minizip
find_package(unofficial-minizip CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC unofficial::minizip::minizip)

## centitoml
find_package(centitoml CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC centitoml::centitoml)

## ffmpeg
find_package(FFMPEG COMPONENTS AVCODEC AVFORMAT AVUTIL SWRESAMPLE REQUIRED)
target_include_directories(keeperfx PUBLIC ${FFMPEG_INCLUDE_DIRS})
target_link_directories(keeperfx PUBLIC ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(keeperfx PUBLIC ${FFMPEG_LIBRARIES})
if(WIN32)
    target_link_libraries(keeperfx PUBLIC bcrypt)
endif()

## luajit
find_library(LUAJIT_LIB NAMES lua51 luajit-5.1 luajit REQUIRED)
find_path(LUAJIT_INCLUDE NAMES lua.h PATH_SUFFIXES luajit luajit-5.1 luajit-2.1 REQUIRED)
target_link_libraries(keeperfx PUBLIC ${LUAJIT_LIB})
target_include_directories(keeperfx PUBLIC ${LUAJIT_INCLUDE})

## openal
find_package(OpenAL CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC OpenAL::OpenAL)

## miniupnpc
find_package(miniupnpc CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC miniupnpc::miniupnpc)

## libnatpmp
find_package(libnatpmp CONFIG REQUIRED)
target_link_libraries(keeperfx PUBLIC libnatpmp::libnatpmp)

## curl (matchmaking WebSocket client) — enabled by Dependencies.cmake detection
if(KEEPERFX_MATCHMAKING)
    find_package(CURL CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC CURL::libcurl)
endif()

