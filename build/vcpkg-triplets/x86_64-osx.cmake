# vcpkg triplet: macOS x86_64 (Intel) native build with static library linkage.
# Used by the macos-x86_64-release CMake preset for native Intel builds.
#
# Targets macOS 11.0 (Big Sur) minimum for broad compatibility with modern Intel Macs.
# Forces static linkage for vcpkg-provided libraries where supported.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)
