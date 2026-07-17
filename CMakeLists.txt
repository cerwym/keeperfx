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
set(VER_RELEASE     2)
set(VER_BUILD       0)

# Packaging parameters (override via -DBUILD_NUMBER=<n> -DPACKAGE_SUFFIX=<s>)
set(BUILD_NUMBER    "0"  CACHE STRING "Monotonic build number (git rev-list count)")
set(PACKAGE_SUFFIX  ""   CACHE STRING "Distribution suffix appended to package filename (e.g. Alpha, Release)")

set(VER_STRING "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${BUILD_NUMBER} ${PACKAGE_SUFFIX}")

# Generate version header
set(KEEPERFX_VER_DEFS_H_IN ${CMAKE_SOURCE_DIR}/build/ver_defs.h.in)
set(KEEPERFX_VER_DEFS_H_OUT ${CMAKE_SOURCE_DIR}/src/ver_defs.h)
configure_file(${KEEPERFX_VER_DEFS_H_IN} ${KEEPERFX_VER_DEFS_H_OUT})

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Build Options
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
option(KEEPERFX_USE_NSIGHT "Launch with Nvidia Nsight Graphics for GPU debugging (MSVC only)" OFF)
option(KEEPERFX_HVLOG     "Build with high-verbosity logging (BFDEBUG_LEVEL=10)" OFF)

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
    add_subdirectory(build/tools)
endif()


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Platform-specific setup
# See: build/cmake/modules/Platform*.cmake
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
if(PLATFORM_VITA)
    include(PlatformVita)
endif()

if(PLATFORM_3DS)
    include(Platform3DS)
endif()

if(PLATFORM_SWITCH)
    include(PlatformSwitch)
endif()

if(PLATFORM_WII_U)
    include(PlatformWiiU)
endif()

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Packaging (CPack)
# See: build/cmake/modules/Packaging.cmake
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
include(Packaging)
