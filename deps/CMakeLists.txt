cmake_minimum_required(VERSION 3.20)

function(kfx_resolve_dep_archive archive_name archive_url)
	set(archive_dst "${CMAKE_SOURCE_DIR}/deps/${archive_name}")

	if(NOT EXISTS "${archive_dst}" AND DEFINED ENV{KFX_DEPS_CACHE} AND NOT "$ENV{KFX_DEPS_CACHE}" STREQUAL "")
		set(archive_cache_src "$ENV{KFX_DEPS_CACHE}/${archive_name}")
		if(EXISTS "${archive_cache_src}")
			message(STATUS "Using cached dependency archive: ${archive_name}")
			execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${archive_cache_src}" "${archive_dst}")
		endif()
	endif()

	if(NOT EXISTS "${archive_dst}")
		file(DOWNLOAD "${archive_url}" "${archive_dst}" SHOW_PROGRESS)
	endif()

	file(SIZE "${archive_dst}" archive_size)
	if(archive_size EQUAL 0)
		message(FATAL_ERROR "Dependency archive ${archive_name} is empty. URL: ${archive_url}")
	endif()
endfunction()

# -------------------------------------------------------------------
# Download pre-built archives — Windows MinGW cross-compile only
# MSVC uses vcpkg; Linux/macOS use system packages or FetchContent
# -------------------------------------------------------------------
if(WIN32 AND NOT MSVC)
	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/enet6/lib/libenet6.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/enet6)
		kfx_resolve_dep_archive("enet6-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/20260310/enet6-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/enet6-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/enet6)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/zlib/libz.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/zlib)
		kfx_resolve_dep_archive("zlib-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/initial/zlib-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/zlib-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/zlib)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/spng/libspng.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/spng)
		kfx_resolve_dep_archive("spng-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/initial/spng-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/spng-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/spng)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/astronomy/libastronomy.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/astronomy)
		kfx_resolve_dep_archive("astronomy-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/initial/astronomy-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/astronomy-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/astronomy)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/centijson/libjson.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/centijson)
		kfx_resolve_dep_archive("centijson-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/initial/centijson-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/centijson-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/centijson)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/ffmpeg/libavcodec/libavcodec.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/ffmpeg)
		kfx_resolve_dep_archive("ffmpeg-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/initial/ffmpeg-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/ffmpeg-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/ffmpeg)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/luajit/lib/libluajit.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/luajit)
		kfx_resolve_dep_archive("luajit-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/20250418/luajit-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/luajit-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/luajit)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/openal/libOpenAL32.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/openal)
		kfx_resolve_dep_archive("openal-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/20260310/openal-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/openal-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/openal)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/miniupnpc/libminiupnpc.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/miniupnpc)
		kfx_resolve_dep_archive("miniupnpc-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/20260102/miniupnpc-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/miniupnpc-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/miniupnpc)
	endif()

	if(NOT EXISTS ${CMAKE_SOURCE_DIR}/deps/libnatpmp/libnatpmp.a)
		file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/libnatpmp)
		kfx_resolve_dep_archive("libnatpmp-mingw32.tar.gz" "https://github.com/dkfans/kfx-deps/releases/download/20260102/libnatpmp-mingw32.tar.gz")
		execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_SOURCE_DIR}/deps/libnatpmp-mingw32.tar.gz WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/deps/libnatpmp)
	endif()
endif()

# -------------------------------------------------------------------
# Linux native — fetch source for libs not available as system packages
# enet6 and astronomy have no system packages; build from source via FetchContent
# centijson source is bundled in deps/centijson/
# -------------------------------------------------------------------
if(UNIX AND NOT APPLE)
	find_package(PkgConfig REQUIRED)

	include(FetchContent)

	FetchContent_Declare(enet6_src
		GIT_REPOSITORY https://github.com/SirLynix/enet6.git
		GIT_TAG        main
		GIT_SHALLOW    ON
	)

	FetchContent_Declare(astronomy_src
		GIT_REPOSITORY https://github.com/cosinekitty/astronomy.git
		GIT_TAG        main
		GIT_SHALLOW    ON
	)

	FetchContent_MakeAvailable(enet6_src astronomy_src)

	# astronomy is a single .c + .h; build as a static library
	add_library(astronomy_from_src STATIC ${astronomy_src_SOURCE_DIR}/source/c/astronomy.c)
	set_property(TARGET astronomy_from_src PROPERTY C_STANDARD 11)
	target_include_directories(astronomy_from_src PUBLIC ${astronomy_src_SOURCE_DIR}/source/c)
	target_compile_options(astronomy_from_src PRIVATE -w)
endif()

# -------------------------------------------------------------------
# Library linking
#
# (MSVC OR APPLE): Windows MSVC + macOS — both use vcpkg
# WIN32 (non-MSVC): MinGW cross-compile targeting Windows — prebuilt archives
# else:             Linux native — system packages + source builds
# -------------------------------------------------------------------

## enet6
if(MSVC OR APPLE)
    find_package(enet6 CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC enet6::enet6)
    target_link_libraries(keeperfx_hvlog PUBLIC enet6::enet6)
elseif(WIN32)
    add_library(enet_static STATIC IMPORTED)
    set_target_properties(enet_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/enet6/lib/libenet6.a)
    set_target_properties(enet_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/enet6/include)
    target_link_libraries(enet_static INTERFACE ws2_32 winmm)
    target_link_libraries(keeperfx PUBLIC enet_static)
    target_link_libraries(keeperfx_hvlog PUBLIC enet_static)
else()
    # Linux: enet6 target provided by FetchContent above
    target_link_libraries(keeperfx PUBLIC enet6)
    target_link_libraries(keeperfx_hvlog PUBLIC enet6)
endif()

## spng
if(MSVC OR APPLE)
    find_package(SPNG CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)
    target_link_libraries(keeperfx_hvlog PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)
elseif(WIN32)
    add_library(spng_static STATIC IMPORTED)
    set_target_properties(spng_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/spng/libspng.a)
    set_target_properties(spng_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/spng/include)
    target_link_libraries(keeperfx PUBLIC spng_static)
    target_link_libraries(keeperfx_hvlog PUBLIC spng_static)
else()
    # Linux: libspng-dev system package
    pkg_check_modules(SPNG REQUIRED spng)
    target_include_directories(keeperfx PUBLIC ${SPNG_INCLUDE_DIRS})
    target_include_directories(keeperfx_hvlog PUBLIC ${SPNG_INCLUDE_DIRS})
    target_link_libraries(keeperfx PUBLIC ${SPNG_LIBRARIES})
    target_link_libraries(keeperfx_hvlog PUBLIC ${SPNG_LIBRARIES})
endif()

## centijson
if(MSVC OR APPLE)
    find_package(centijson CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC centijson::centijson)
    target_link_libraries(keeperfx_hvlog PUBLIC centijson::centijson)
elseif(WIN32)
    add_library(centijson_static STATIC IMPORTED)
    set_target_properties(centijson_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/centijson/libjson.a)
    set_target_properties(centijson_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/centijson/include)
    target_link_libraries(keeperfx PUBLIC centijson_static)
    target_link_libraries(keeperfx_hvlog PUBLIC centijson_static)
else()
    # Linux: compile from bundled source in deps/centijson/
    add_library(centijson_static STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/centijson/json.c
        ${CMAKE_CURRENT_SOURCE_DIR}/centijson/json-dom.c
        ${CMAKE_CURRENT_SOURCE_DIR}/centijson/json-ptr.c
        ${CMAKE_CURRENT_SOURCE_DIR}/centijson/value.c
    )
    target_include_directories(centijson_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/centijson/include)
    target_link_libraries(keeperfx PUBLIC centijson_static)
    target_link_libraries(keeperfx_hvlog PUBLIC centijson_static)
endif()

## astronomy
if(MSVC OR APPLE)
    find_package(astronomy CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC astronomy::astronomy)
    target_link_libraries(keeperfx_hvlog PUBLIC astronomy::astronomy)
elseif(WIN32)
    add_library(astronomy_static STATIC IMPORTED)
    set_target_properties(astronomy_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/astronomy/libastronomy.a)
    set_target_properties(astronomy_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/astronomy/include)
    target_link_libraries(keeperfx PUBLIC astronomy_static)
    target_link_libraries(keeperfx_hvlog PUBLIC astronomy_static)
else()
    # Linux: astronomy_from_src target built from FetchContent above
    target_link_libraries(keeperfx PUBLIC astronomy_from_src)
    target_link_libraries(keeperfx_hvlog PUBLIC astronomy_from_src)
endif()

## zlib
if(MSVC OR APPLE)
    find_package(ZLIB REQUIRED)
    target_link_libraries(keeperfx PUBLIC ZLIB::ZLIB)
    target_link_libraries(keeperfx_hvlog PUBLIC ZLIB::ZLIB)
elseif(WIN32)
    add_library(zlib_static STATIC IMPORTED)
    set_target_properties(zlib_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/zlib/libz.a)
    set_target_properties(zlib_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include)
    target_link_libraries(keeperfx PUBLIC zlib_static)
    target_link_libraries(keeperfx_hvlog PUBLIC zlib_static)
else()
    # Linux: zlib1g-dev system package
    find_package(ZLIB REQUIRED)
    target_link_libraries(keeperfx PUBLIC ZLIB::ZLIB)
    target_link_libraries(keeperfx_hvlog PUBLIC ZLIB::ZLIB)
endif()

# Add minizip
if(MSVC OR APPLE)
    find_package(unofficial-minizip CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC unofficial::minizip::minizip)
    target_link_libraries(keeperfx_hvlog PUBLIC unofficial::minizip::minizip)
elseif(WIN32)
    add_library(minizip_static STATIC IMPORTED)
    set_target_properties(minizip_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/zlib/libminizip.a)
    set_target_properties(minizip_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/zlib/include)
    target_link_libraries(minizip_static INTERFACE zlib_static)
    target_link_libraries(keeperfx PUBLIC minizip_static)
    target_link_libraries(keeperfx_hvlog PUBLIC minizip_static)
else()
    # Linux: libminizip-dev system package
    pkg_check_modules(MINIZIP REQUIRED minizip)
    target_include_directories(keeperfx PUBLIC ${MINIZIP_INCLUDE_DIRS})
    target_include_directories(keeperfx_hvlog PUBLIC ${MINIZIP_INCLUDE_DIRS})
    target_link_libraries(keeperfx PUBLIC ${MINIZIP_LIBRARIES})
    target_link_libraries(keeperfx_hvlog PUBLIC ${MINIZIP_LIBRARIES})
endif()

## centitoml
add_library(centitoml OBJECT "centitoml/toml_api.c")
if(MSVC OR APPLE)
    target_link_libraries(centitoml PUBLIC centijson::centijson)
else()
    target_link_libraries(centitoml PUBLIC centijson_static)
endif()
target_include_directories(centitoml INTERFACE "centitoml")
target_link_libraries(keeperfx PUBLIC centitoml)
target_link_libraries(keeperfx_hvlog PUBLIC centitoml)

## ffmpeg
if(MSVC OR APPLE)
    find_package(FFMPEG COMPONENTS AVCODEC AVFORMAT AVUTIL SWRESAMPLE REQUIRED)
    target_include_directories(keeperfx PUBLIC ${FFMPEG_INCLUDE_DIRS})
    target_include_directories(keeperfx_hvlog PUBLIC ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(keeperfx PUBLIC ${FFMPEG_LIBRARY_DIRS})
    target_link_directories(keeperfx_hvlog PUBLIC ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(keeperfx PUBLIC ${FFMPEG_LIBRARIES})
    target_link_libraries(keeperfx_hvlog PUBLIC ${FFMPEG_LIBRARIES})
    if(MSVC)
        # bcrypt is required by ffmpeg on Windows only
        target_link_libraries(keeperfx PUBLIC bcrypt)
        target_link_libraries(keeperfx_hvlog PUBLIC bcrypt)
    endif()
elseif(WIN32)
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

    target_link_libraries(keeperfx PUBLIC libavformat_static libavcodec_static libswresample_static libavutil_static bcrypt)
    target_link_libraries(keeperfx_hvlog PUBLIC libavformat_static libavcodec_static libswresample_static libavutil_static bcrypt)
else()
    # Linux: system ffmpeg packages
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
    foreach(_tgt keeperfx keeperfx_hvlog)
        target_include_directories(${_tgt} PUBLIC ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWRESAMPLE_INCLUDE_DIRS})
        target_link_libraries(${_tgt} PUBLIC ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWRESAMPLE_LIBRARIES})
    endforeach()
endif()

## luajit
if(MSVC OR APPLE)
    find_library(LUAJIT_LIB NAMES lua51 REQUIRED)
    find_path(LUAJIT_INCLUDE NAMES lua.h PATH_SUFFIXES luajit REQUIRED)
    target_link_libraries(keeperfx PUBLIC ${LUAJIT_LIB})
    target_link_libraries(keeperfx_hvlog PUBLIC ${LUAJIT_LIB})
    target_include_directories(keeperfx PUBLIC ${LUAJIT_INCLUDE})
    target_include_directories(keeperfx_hvlog PUBLIC ${LUAJIT_INCLUDE})
elseif(WIN32)
    add_library(luajit_static STATIC IMPORTED)
    set_target_properties(luajit_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/luajit/lib/libluajit.a)
    set_target_properties(luajit_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/luajit/include)
    target_link_libraries(keeperfx PUBLIC luajit_static)
    target_link_libraries(keeperfx_hvlog PUBLIC luajit_static)
else()
    # Linux: libluajit-5.1-dev system package
    pkg_check_modules(LUAJIT REQUIRED luajit)
    target_include_directories(keeperfx PUBLIC ${LUAJIT_INCLUDE_DIRS})
    target_include_directories(keeperfx_hvlog PUBLIC ${LUAJIT_INCLUDE_DIRS})
    target_link_libraries(keeperfx PUBLIC ${LUAJIT_LIBRARIES})
    target_link_libraries(keeperfx_hvlog PUBLIC ${LUAJIT_LIBRARIES})
endif()

## openal
if(MSVC OR APPLE)
    find_package(OpenAL CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC OpenAL::OpenAL)
    target_link_libraries(keeperfx_hvlog PUBLIC OpenAL::OpenAL)
elseif(WIN32)
    # MinGW — static OpenAL32 from kfx-deps
    add_library(openal_static STATIC IMPORTED)
    set_target_properties(openal_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/openal/libOpenAL32.a)
    set_target_properties(openal_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/openal/include)
    set_target_properties(openal_static PROPERTIES INTERFACE_COMPILE_DEFINITIONS "AL_LIBTYPE_STATIC=1")
    target_link_libraries(openal_static INTERFACE avrt)
    target_link_libraries(keeperfx PUBLIC openal_static)
    target_link_libraries(keeperfx_hvlog PUBLIC openal_static)
else()
    # Linux: libopenal-dev system package
    find_package(OpenAL REQUIRED)
    target_link_libraries(keeperfx PUBLIC OpenAL::OpenAL)
    target_link_libraries(keeperfx_hvlog PUBLIC OpenAL::OpenAL)
endif()

## miniupnpc
if(MSVC OR APPLE)
    find_package(miniupnpc CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC miniupnpc::miniupnpc)
    target_link_libraries(keeperfx_hvlog PUBLIC miniupnpc::miniupnpc)
elseif(WIN32)
    add_library(miniupnpc_static STATIC IMPORTED)
    set_target_properties(miniupnpc_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/miniupnpc/libminiupnpc.a)
    set_target_properties(miniupnpc_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/miniupnpc/include)
    target_link_libraries(keeperfx PUBLIC miniupnpc_static)
    target_link_libraries(keeperfx_hvlog PUBLIC miniupnpc_static)
else()
    # Linux: libminiupnpc-dev system package
    pkg_check_modules(MINIUPNPC REQUIRED miniupnpc)
    target_include_directories(keeperfx PUBLIC ${MINIUPNPC_INCLUDE_DIRS})
    target_include_directories(keeperfx_hvlog PUBLIC ${MINIUPNPC_INCLUDE_DIRS})
    target_link_libraries(keeperfx PUBLIC ${MINIUPNPC_LIBRARIES})
    target_link_libraries(keeperfx_hvlog PUBLIC ${MINIUPNPC_LIBRARIES})
endif()

## libnatpmp
if(MSVC OR APPLE)
    find_package(libnatpmp CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC libnatpmp::libnatpmp)
    target_link_libraries(keeperfx_hvlog PUBLIC libnatpmp::libnatpmp)
elseif(WIN32)
    add_library(natpmp_static STATIC IMPORTED)
    set_target_properties(natpmp_static PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libnatpmp/libnatpmp.a)
    set_target_properties(natpmp_static PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/libnatpmp/include)
    target_link_libraries(natpmp_static INTERFACE iphlpapi)
    target_link_libraries(keeperfx PUBLIC natpmp_static)
    target_link_libraries(keeperfx_hvlog PUBLIC natpmp_static)
else()
    # Linux: libnatpmp-dev system package
    find_library(NATPMP_LIB NAMES natpmp REQUIRED)
    find_path(NATPMP_INCLUDE NAMES natpmp.h REQUIRED)
    target_link_libraries(keeperfx PUBLIC ${NATPMP_LIB})
    target_link_libraries(keeperfx_hvlog PUBLIC ${NATPMP_LIB})
    target_include_directories(keeperfx PUBLIC ${NATPMP_INCLUDE})
    target_include_directories(keeperfx_hvlog PUBLIC ${NATPMP_INCLUDE})
endif()

## curl
if(MSVC)
    find_package(CURL CONFIG REQUIRED)
    target_link_libraries(keeperfx PUBLIC CURL::libcurl)
    target_link_libraries(keeperfx_hvlog PUBLIC CURL::libcurl)
endif()
