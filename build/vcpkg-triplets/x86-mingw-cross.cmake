# vcpkg triplet: Windows x86 cross-compiled from Linux using MinGW-w64
# Used by the windows-x86-* CMake presets (keeperfx-build-mingw32 Docker image).
#
# Chainloads build/cmake/toolchains/mingw32-posix.cmake so that both vcpkg
# package builds and the main KeeperFX build use the same cross-compiler.

set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

get_filename_component(_triplet_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${_triplet_dir}/../cmake/toolchains/mingw32-posix.cmake")
