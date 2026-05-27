# vcpkg triplet: Linux x64 native build with static library linkage.
# Used by the linux-x64-* CMake presets (keeperfx-build-linux Docker image).
#
# Mirrors the community x64-linux triplet but forces static linkage for
# all vcpkg-provided libraries so the binary ships without extra .so files.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)     # libc / libstdc++ are always dynamic on Linux
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
