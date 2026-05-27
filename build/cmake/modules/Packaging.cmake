# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Packaging.cmake — CPack configuration for KeeperFX distribution packages
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#
# Usage (full game distribution package):
#   1. Configure (pass BUILD_NUMBER and PACKAGE_SUFFIX):
#        cmake --preset windows-x86-release -DBUILD_NUMBER=1234 -DPACKAGE_SUFFIX=Alpha
#   2. Build the binary:
#        cmake --build --preset windows-x86-release
#   3. Assemble game data (gfx/lang/sfx pipeline) into pkg/:
#        make BUILD_NUMBER=1234 PACKAGE_SUFFIX=Alpha pkg-assemble
#   4. Create the distribution archive:
#        cmake --build --preset windows-x86-release --target package
#
# The game data pipeline (step 3) requires GNU Make and the KeeperFX tool suite
# (pngpal2raw, po2ngdat, sndbanker, etc.). These are pre-installed in the
# KeeperFX Docker build images (ghcr.io/cerwym/keeperfx-build-mingw32:latest, etc.)
#
# Output:
#   pkg/keeperfx-1_3_2_1234-Alpha-patch.7z  (Windows / MinGW cross-compile)
#   pkg/keeperfx-1_3_2_1234-Alpha-patch.tar.gz  (Linux / macOS)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# Homebrew platforms (Vita, 3DS, Switch, Wii U) use their own SDK packaging
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH OR PLATFORM_WII_U)
    return()
endif()

# ━━━ Package metadata ━━━
set(CPACK_PACKAGE_NAME      "keeperfx")
set(CPACK_PACKAGE_VENDOR    "KeeperFX Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "KeeperFX — Free implementation of Dungeon Keeper")
set(CPACK_PACKAGE_VERSION   "${VER_MAJOR}.${VER_MINOR}.${VER_RELEASE}.${BUILD_NUMBER}")
set(CPACK_PACKAGE_VERSION_MAJOR "${VER_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${VER_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${VER_RELEASE}")

# ━━━ Archive filename ━━━
# Matches the Makefile convention: keeperfx-1_3_2_BUILD-SUFFIX-patch
if(PACKAGE_SUFFIX AND NOT "${PACKAGE_SUFFIX}" STREQUAL "")
    set(CPACK_PACKAGE_FILE_NAME
        "keeperfx-${VER_MAJOR}_${VER_MINOR}_${VER_RELEASE}_${BUILD_NUMBER}-${PACKAGE_SUFFIX}-patch")
else()
    set(CPACK_PACKAGE_FILE_NAME
        "keeperfx-${VER_MAJOR}_${VER_MINOR}_${VER_RELEASE}_${BUILD_NUMBER}-patch")
endif()

# Place generated archives in the source-tree pkg/ directory
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/pkg")

# Flat archive layout (no top-level directory prefix)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# ━━━ Generator selection ━━━
# 7Z on Windows and MinGW cross-compile targets; TGZ elsewhere
if(WIN32 OR MINGW OR CMAKE_CROSSCOMPILING OR CMAKE_SYSTEM_NAME STREQUAL "MinGW")
    set(CPACK_GENERATOR "7Z")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# ━━━ Install rules ━━━

# The main game binary (from cmake build output)
install(TARGETS keeperfx RUNTIME DESTINATION .)

# Game data assembled by the make data pipeline.
# Run "make ... pkg-assemble" before "cmake --build --target package".
# The install code runs at pack time so pkg/ is evaluated then, not at configure time.
install(CODE "
    set(_pkg_src \"${CMAKE_SOURCE_DIR}/pkg\")
    if(EXISTS \"\${_pkg_src}\")
        file(GLOB_RECURSE _pkg_files
            LIST_DIRECTORIES false
            RELATIVE \"\${_pkg_src}\"
            \"\${_pkg_src}/*\")
        foreach(_f IN LISTS _pkg_files)
            if(NOT _f MATCHES \"keeperfx.*\\\\.(7z|tar\\\\.gz|tgz)\$\")
                get_filename_component(_dir \"\${_f}\" DIRECTORY)
                file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/\${_dir}\")
                file(COPY \"\${_pkg_src}/\${_f}\"
                    DESTINATION \"\${CMAKE_INSTALL_PREFIX}/\${_dir}\")
            endif()
        endforeach()
    endif()
")

include(CPack)
