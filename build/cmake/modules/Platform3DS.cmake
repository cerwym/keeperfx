# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Platform3DS.cmake — Nintendo 3DS platform setup
# Included from the root CMakeLists.txt after BuildTargets defines the executables.
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

if(NOT DEFINED ENV{DEVKITARM})
    message(FATAL_ERROR "DEVKITARM environment variable not set. Install devkitARM from devkitPro.")
endif()

# Optimization flags (architecture-specific flags are set by the toolchain)
foreach(_target IN LISTS KFX_TARGETS)
    target_compile_options(${_target} PRIVATE -ffast-math)
endforeach()

# citro3d library (PICA200 hardware abstraction)
find_library(CITRO3D_LIB citro3d HINTS "${DEVKITPRO}/libctru/lib" REQUIRED)
find_library(CTR_LIB     ctru    HINTS "${DEVKITPRO}/libctru/lib" REQUIRED)
find_library(M_LIB       m       HINTS "$ENV{DEVKITARM}/lib")

foreach(_target IN LISTS KFX_TARGETS)
    target_link_libraries(${_target} PRIVATE ${CITRO3D_LIB} ${CTR_LIB} ${M_LIB})
endforeach()

# Debug symbols for crash analysis
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_target IN LISTS KFX_TARGETS)
        target_compile_options(${_target} PRIVATE -g)
    endforeach()
endif()
