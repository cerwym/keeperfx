# vcpkg triplet: Windows x86 cross-compiled from Linux using MinGW-w64
# Used by the windows-x86-* CMake presets (keeperfx-build-mingw32 Docker image).
#
# Chainloads build/cmake/toolchains/mingw32-posix.cmake so that both vcpkg
# package builds and the main KeeperFX build use the same cross-compiler.

set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

# GCC 13 for i686-w64-mingw32 ships avx512bf16intrin.h / avx512fp16vlintrin.h
# which unconditionally reference _Float16 / __bf16 — types not supported on
# 32-bit x86 targets.  Disabling AVX-512F prevents GCC from including those
# headers, fixing compilation of any port that pulls in <immintrin.h> (e.g. libspng).
set(VCPKG_C_FLAGS   "-mno-avx512f")
set(VCPKG_CXX_FLAGS "-mno-avx512f")

get_filename_component(_triplet_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_triplet_dir}/../cmake/toolchains/mingw32.cmake")
