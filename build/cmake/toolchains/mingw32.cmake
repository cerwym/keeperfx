# Cross-compile toolchain: Windows x86 (i686-w64-mingw32, win32 threads)
# Used by the windows-x86-* CMake presets when building inside the
# keeperfx-build-mingw32 Docker image on a Linux host.
set(CMAKE_SYSTEM_NAME Windows)
set(TOOLCHAIN_PREFIX i686-w64-mingw32)
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)
set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# BOTH for LIBRARY and INCLUDE: vcpkg cmake-wrappers use explicit absolute
# paths to vcpkg_installed with NO_DEFAULT_PATH. With ONLY mode cmake re-roots
# these absolute paths through the sysroot (producing invalid paths). BOTH lets
# cmake also search the direct absolute path, so vcpkg package discovery works.
# Standard Linux paths are excluded by NO_DEFAULT_PATH in the cmake wrappers.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

# GCC 13 for i686-w64-mingw32 enables __AVX512BF16__, __AVXNECONVERT__ and
# __AVX512FP16__ by default even on 32-bit x86 targets.  These extensions
# reference _Float16 / __bf16 types that are not supported on i686, causing
# compile errors in any translation unit that includes <immintrin.h> (e.g. libspng).
# Setting CMAKE_C/CXX_FLAGS_INIT ensures these flags are present in all cmake
# projects built with this toolchain, including vcpkg port builds where
# VCPKG_C_FLAGS from the triplet may be overridden by the port's own cmake logic.
set(_mingw32_no_broken_exts "-mno-avx512f -mno-avx512bf16 -mno-avxneconvert -mno-avx512fp16")
set(CMAKE_C_FLAGS_INIT   "${_mingw32_no_broken_exts}")
set(CMAKE_CXX_FLAGS_INIT "${_mingw32_no_broken_exts}")
