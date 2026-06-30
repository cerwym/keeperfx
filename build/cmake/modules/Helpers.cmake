# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Helpers.cmake — Common functions and macros
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# Log a status message with [PREFIX] for clarity
function(kfx_status PREFIX MESSAGE)
    message(STATUS "[${PREFIX}] ${MESSAGE}")
endfunction()

# Apply common compilation flags to a target based on platform
function(apply_keeperfx_warnings TARGET)
    if(MSVC)
        # MSVC: use /W3, suppress common cross-platform noise
        target_compile_options(${TARGET} PRIVATE
            /W3
            /wd4996   # deprecated functions
            /wd4267   # size_t conversion
            /wd4244   # float/int conversion
            /wd4305   # truncation
            /wd4018   # signed/unsigned mismatch
            /wd4146   # unary minus on unsigned
            /wd4101   # unreferenced local variable
        )
        return()
    endif()

    set(WARNFLAGS_COMMON -Wall -W -Wno-sign-compare -Wno-unused-parameter -Wno-strict-aliasing -Wno-unknown-pragmas -Werror)
    set(WARNFLAGS_C   ${WARNFLAGS_COMMON} -Wshadow)
    set(WARNFLAGS_CXX ${WARNFLAGS_COMMON})

    if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
        # ARM/AArch64 cross-compile: no x86-specific flags
        target_compile_options(${TARGET} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${WARNFLAGS_C} -Wno-char-subscripts>
            $<$<COMPILE_LANGUAGE:CXX>:${WARNFLAGS_CXX} -Wno-char-subscripts>
        )
    else()
        # Desktop: MinGW or native Linux
        # Use correct march for the target processor; cross-compile to x86 uses i686
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i486|i586|i686|x86)$")
            set(GNU_COMPILER_FLAGS -march=i686 -fno-omit-frame-pointer -fmessage-length=0)
        else()
            set(GNU_COMPILER_FLAGS -march=x86-64 -fno-omit-frame-pointer -fmessage-length=0)
        endif()
        # MinGW cross-compile: GCC's -Wformat checker uses Windows (ms_printf) rules
        # and rejects %zu/%zd even with __USE_MINGW_ANSI_STDIO=1 on some toolchain
        # versions. Suppress format warnings on MinGW to avoid false positives.
        if(MINGW OR (CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows"))
            list(APPEND WARNFLAGS_C   -Wno-format)
            list(APPEND WARNFLAGS_CXX -Wno-format)
        endif()
        target_compile_options(${TARGET} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${WARNFLAGS_C} ${GNU_COMPILER_FLAGS}>
            $<$<COMPILE_LANGUAGE:CXX>:${WARNFLAGS_CXX} ${GNU_COMPILER_FLAGS}>
        )
    endif()
endfunction()

# Apply common link options to a target based on platform
function(apply_keeperfx_link_flags TARGET)
    if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
        # Homebrew platforms: no special link flags needed
        return()
    endif()

    if(MSVC)
        # MSVC: Windows subsystem, no GCC-style flags.
        # /MANIFEST:NO disables the auto-generated manifest because keeperfx_stdres.rc
        # already embeds the manifest via RT_MANIFEST, and duplicate manifests cause LNK error.
        # /INCREMENTAL is required for Hot Reload (Edit and Continue) in Debug builds.
        # /INCREMENTAL:NO is required when ASan is active (/fsanitize=address is incompatible
        # with incremental linking).
        if(KEEPERFX_SANITIZERS)
            target_link_options(${TARGET} PRIVATE
                /SUBSYSTEM:WINDOWS
                /MANIFEST:NO
                /INCREMENTAL:NO
            )
        else()
            target_link_options(${TARGET} PRIVATE
                /SUBSYSTEM:WINDOWS
                /MANIFEST:NO
                $<$<CONFIG:Debug>:/INCREMENTAL>
            )
        endif()
        return()
    endif()
    
    if(WIN32 OR MINGW)
        # MinGW: Windows subsystem + map file
        target_link_options(${TARGET} PRIVATE -mwindows -Wl,--enable-auto-import -Wl,-Map,${TARGET}.map)
        target_link_libraries(${TARGET} PUBLIC -static stdc++ winpthread -dynamic)
        # Use LLVM linker (LLD) for faster linking
        set_property(TARGET ${TARGET} PROPERTY LINKER_TYPE LLD)
    else()
        # Linux: pthreads + map file
        target_link_options(${TARGET} PRIVATE -Wl,-Map,${TARGET}.map)
        find_package(Threads REQUIRED)
        target_link_libraries(${TARGET} PUBLIC Threads::Threads)
    endif()
endfunction()

# Apply system libraries (Windows only: imagehlp, dbghelp, ole32, uuid, winmm, ws2_32, opengl32, dwmapi)
function(apply_windows_system_libs TARGET)
    if(WIN32)
        target_link_libraries(${TARGET} PRIVATE imagehlp dbghelp ole32 uuid winmm ws2_32 opengl32 dxgi dwmapi)
    endif()
endfunction()
