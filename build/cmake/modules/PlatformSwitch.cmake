# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# PlatformSwitch.cmake — Nintendo Switch platform setup
# Included from the root CMakeLists.txt after BuildTargets defines the executables.
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

if(NOT DEFINED ENV{DEVKITA64})
    if(DEVKITPRO)
        set(DEVKITA64 "${DEVKITPRO}/devkitA64")
        kfx_status("PLATFORM" "DEVKITA64 inferred from DEVKITPRO: ${DEVKITA64}")
    else()
        message(FATAL_ERROR "DEVKITA64 environment variable not set and DEVKITPRO not found. Install devkitA64 from devkitPro.")
    endif()
endif()

include_directories($ENV{DEVKITPRO}/portlibs/switch/include)

# Optimization flags (architecture-specific flags are set by the toolchain)
foreach(_target IN LISTS KFX_TARGETS)
    target_compile_options(${_target} PRIVATE -ffast-math)
endforeach()

# libnx core library
find_library(NX_LIB     nx     HINTS "${DEVKITA64}/lib" REQUIRED)
find_library(M_LIB      m      HINTS "${DEVKITA64}/lib")
find_library(AUDREN_LIB audren HINTS "${DEVKITA64}/lib")

if(AUDREN_LIB)
    foreach(_target IN LISTS KFX_TARGETS)
        target_link_libraries(${_target} PRIVATE ${AUDREN_LIB})
    endforeach()
endif()

foreach(_target IN LISTS KFX_TARGETS)
    target_link_libraries(${_target} PRIVATE ${NX_LIB} ${M_LIB})
endforeach()

# Debug symbols for crash analysis
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    foreach(_target IN LISTS KFX_TARGETS)
        target_compile_options(${_target} PRIVATE -g)
    endforeach()
endif()
