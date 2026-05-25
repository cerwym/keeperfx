# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# PlatformWiiU.cmake — Nintendo Wii U platform setup
# Included from the root CMakeLists.txt after BuildTargets defines the executables.
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

if(NOT DEFINED ENV{DEVKITPPC})
    message(FATAL_ERROR "DEVKITPPC environment variable not set. Install devkitPPC from devkitPro.")
endif()

# Optimization flags (architecture-specific flags are set by the WiiU.cmake toolchain)
foreach(_target IN LISTS KFX_TARGETS)
    target_compile_options(${_target} PRIVATE -ffast-math)
endforeach()

# WUT (Wii U Toolchain) libraries
set(_WUT_LIB_HINTS "${DEVKITPRO}/wut/lib" "${DEVKITPRO}/libwut/lib" "$ENV{DEVKITPPC}/lib")

find_library(GX2_LIB      gx2      HINTS ${_WUT_LIB_HINTS})
find_library(GFD_LIB      gfd      HINTS ${_WUT_LIB_HINTS})
find_library(AX_LIB       ax       HINTS ${_WUT_LIB_HINTS})
find_library(WIIU_LIB     wiiu     HINTS ${_WUT_LIB_HINTS})
find_library(COREINIT_LIB coreinit HINTS ${_WUT_LIB_HINTS})
find_library(M_LIB        m        HINTS ${_WUT_LIB_HINTS})

foreach(_target IN LISTS KFX_TARGETS)
    target_link_libraries(${_target} PRIVATE
        ${GX2_LIB} ${GFD_LIB} ${AX_LIB} ${WIIU_LIB} ${COREINIT_LIB} ${M_LIB})
endforeach()

# Debug symbols for crash analysis
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_target IN LISTS KFX_TARGETS)
        target_compile_options(${_target} PRIVATE -g)
    endforeach()
endif()

# ━━━ Wii U executable format (.rpx) ━━━
# wut_create_rpx is provided by devkitPro's WiiU.cmake toolchain.
if(COMMAND wut_create_rpx)
    wut_create_rpx(keeperfx.rpx keeperfx)
else()
    kfx_status("WII U" "wut_create_rpx not available — install WUT from devkitPro")
endif()
