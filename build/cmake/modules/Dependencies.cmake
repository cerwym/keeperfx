# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Dependencies.cmake — find_package() for all dependencies
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# Desktop platforms (MinGW cross-compile or native Linux/macOS) use vcpkg or system packages
# Homebrew platforms (Vita, 3DS, Switch) use their own SDK bundled dependencies
if(NOT PLATFORM_VITA AND NOT PLATFORM_3DS AND NOT PLATFORM_SWITCH)
    # ━━━ SDL2 & Graphics ━━━
    # MinGW cross-compile: force static SDL2 libs (no runtime DLLs) and enable
    # POSIX-compatible printf (so %zu, %zd etc. work for size_t formatting).
    if(MINGW OR CMAKE_CROSSCOMPILING)
        set(SDL2_USE_STATIC_LIBS ON)
        set(SDL2IMAGE_STATIC ON)
        set(SDL2MIXER_STATIC ON)
        set(SDL2NET_STATIC ON)
        add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
    endif()
    
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
    if(KEEPERFX_NETWORKING)
        find_package(SDL2_net CONFIG REQUIRED)
    endif()

    # ━━━ OpenGL Renderer (optional) ━━━
    if(KEEPERFX_RENDERER_OPENGL)
        find_package(glad CONFIG QUIET)
        if(glad_FOUND)
            kfx_status("DEPS" "OpenGL renderer backend enabled (glad found)")
            add_compile_definitions(RENDERER_OPENGL_ENABLED=1)
        else()
            kfx_status("DEPS" "OpenGL renderer backend disabled (glad not found; install via vcpkg)")
            set(KEEPERFX_RENDERER_OPENGL OFF)
        endif()
    endif()

    # ━━━ Vulkan Renderer (optional) ━━━
    if(KEEPERFX_RENDERER_VULKAN)
        find_package(Vulkan QUIET)
        find_package(vk-bootstrap CONFIG QUIET)
        find_package(VulkanMemoryAllocator CONFIG QUIET)
        if(Vulkan_FOUND AND vk-bootstrap_FOUND)
            kfx_status("DEPS" "Vulkan renderer backend enabled (Vulkan + vk-bootstrap found)")
            add_compile_definitions(RENDERER_VULKAN_ENABLED=1)
            set(KFX_VK_LINK_LIBS Vulkan::Vulkan vk-bootstrap::vk-bootstrap)
            if(VulkanMemoryAllocator_FOUND)
                list(APPEND KFX_VK_LINK_LIBS GPUOpen::VulkanMemoryAllocator)
                kfx_status("DEPS" "VulkanMemoryAllocator found and linked")
            else()
                kfx_status("DEPS" "VulkanMemoryAllocator not found — install via vcpkg for full Vulkan support")
            endif()
            find_package(unofficial-shaderc CONFIG QUIET)
            if(unofficial-shaderc_FOUND)
                add_compile_definitions(RENDERER_VK_SHADERC_AVAILABLE=1)
                list(APPEND KFX_VK_LINK_LIBS unofficial::shaderc::shaderc)
                kfx_status("DEPS" "shaderc found and linked (GLSL→SPIR-V at runtime)")
            else()
                kfx_status("DEPS" "shaderc not found — Vulkan shader compilation unavailable")
            endif()
        else()
            kfx_status("DEPS" "Vulkan renderer backend disabled (Vulkan=${Vulkan_FOUND} vk-bootstrap=${vk-bootstrap_FOUND})")
            set(KEEPERFX_RENDERER_VULKAN OFF)
        endif()
    endif()
    
    # ━━━ Audio & Codecs (vcpkg) ━━━
    find_package(FFmpeg MODULE QUIET)
    if(FFmpeg_FOUND)
        kfx_status("DEPS" "FFmpeg found (vcpkg)")
    else()
        kfx_status("DEPS" "FFmpeg not found — ensure vcpkg is bootstrapped and triplet is configured")
    endif()
    
    find_package(OpenAL CONFIG QUIET)
    if(OpenAL_FOUND OR OPENAL_FOUND)
        kfx_status("DEPS" "OpenAL found (vcpkg)")
    else()
        kfx_status("DEPS" "OpenAL not found — ensure vcpkg is bootstrapped and triplet is configured")
    endif()
    
    # ━━━ Networking ━━━
    find_package(enet CONFIG QUIET)
    if(enet_FOUND)
        kfx_status("DEPS" "enet found (vcpkg)")
    else()
        kfx_status("DEPS" "enet not found — ensure vcpkg is bootstrapped and triplet is configured")
    endif()

    # ━━━ Matchmaking (curl WebSocket) ━━━
    option(KEEPERFX_MATCHMAKING "Enable matchmaking client (requires libcurl >= 7.86)" ON)
    if(KEEPERFX_MATCHMAKING)
        find_package(CURL CONFIG QUIET)

        if(CURL_FOUND)
            kfx_status("DEPS" "curl found: matchmaking enabled")
        else()
            kfx_status("DEPS" "curl not found: matchmaking disabled (net_matchmaking.c excluded)")
            set(KEEPERFX_MATCHMAKING OFF CACHE BOOL "" FORCE)
        endif()
    endif()

    find_package(centijson CONFIG QUIET)
    if(centijson_FOUND)
        kfx_status("DEPS" "centijson found (vcpkg)")
    else()
        kfx_status("DEPS" "centijson not found — ensure vcpkg is bootstrapped and triplet is configured")
    endif()

    # ━━━ Tracy Profiler (optional) — built from source via FetchContent ━━━
    # Using FetchContent instead of vcpkg so Tracy is compiled with the same CRT
    # as the rest of the project (MTd in Debug, MT in Release). The vcpkg
    # pre-built lib uses MT regardless, causing LNK2038 CRT mismatch in Debug.
    if(KEEPERFX_TRACY)
        include(FetchContent)
        set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
        set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            tracy
            GIT_REPOSITORY https://github.com/wolfpld/tracy.git
            GIT_TAG        v0.13.1
            GIT_SHALLOW    TRUE
            GIT_PROGRESS   TRUE
        )
        FetchContent_MakeAvailable(tracy)
        kfx_status("PROFILER" "Tracy profiler v0.13.1 fetched from source (TRACY_ENABLE + TRACY_ON_DEMAND)")
    endif()
    
else()
    # ━━━ Homebrew platforms (Vita, 3DS, Switch) ━━━
    kfx_status("DEPS" "Skipping vcpkg — using platform SDK dependencies")
    
    if(PLATFORM_VITA)
        # SDL2 from vitasdk
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VITASDK}/arm-vita-eabi")
        find_package(SDL2 REQUIRED)
    endif()
endif()

# ━━━ Global Definitions (all platforms) ━━━
add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
add_compile_definitions("DEBUG=$<IF:$<CONFIG:Debug>,1,0>")
add_compile_definitions("SPNG_STATIC=1")
