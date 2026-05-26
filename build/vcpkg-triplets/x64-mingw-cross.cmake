# vcpkg triplet: Windows x64 cross-compiled from Linux using MinGW-w64
# Used by the windows-x64-* CMake presets (keeperfx-build-mingw64 Docker image).
#
# Chainloads build/cmake/toolchains/mingw64-posix.cmake so that both vcpkg
# package builds and the main KeeperFX build use the same cross-compiler.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Windows)

get_filename_component(_triplet_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_triplet_dir}/../cmake/toolchains/mingw64-posix.cmake")
