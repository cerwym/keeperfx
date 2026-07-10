# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# BuildTargets.cmake — Target definitions (keeperfx, tests)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

# ━━━ Source File Collection ━━━
# Recursive globs intentionally pick up software rasteriser / blitter sources
# under src/renderer/software/ after their implementation-file relocation.
file(GLOB_RECURSE KEEPERFX_SOURCES_C   CONFIGURE_DEPENDS "src/*.c")
file(GLOB_RECURSE KEEPERFX_SOURCES_CXX CONFIGURE_DEPENDS "src/*.cpp")

# Exclude stub files — added back explicitly when needed
list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_cpu_stub\\.c$")
list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_network_stub\\.c$")

# SDL2 platform files are superseded by SDL3 equivalents — exclude unconditionally
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform_gl_sdl2\\.cpp$")

# Homebrew-only exclusions (Vita/3DS/Switch SDK-specific)
list(FILTER KEEPERFX_SOURCES_C   EXCLUDE REGEX ".*/debugscreen/.*\\.c$")
list(FILTER KEEPERFX_SOURCES_C   EXCLUDE REGEX ".*/platform/vita_malloc_wrap\\.c$")
list(FILTER KEEPERFX_SOURCES_C   EXCLUDE REGEX ".*/platform/vita_sce_diag\\.c$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/vita/.*\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/backends/VitaGPUBackend\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/RendererVita\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/Renderer3DS\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformVita\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/Platform3DS\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformSwitch\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformHomebrewMain\\.cpp$")
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/WindowSystemVita\\.cpp$")

# OpenGL renderer exclusion
if(NOT KEEPERFX_RENDERER_OPENGL)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/RendererOpenGL\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform_gl_sdl3\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/opengl/.*\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/renderer/backends/OpenGLSpriteBackend\\.cpp$")
endif()

# ImGui overlay exclusion
# DevTools.cpp is the always-present facade (no-ops without ImGui) and must stay
# compiled even when the tooling is off, so the rest of the engine can call it
# unconditionally. Everything else under kfx/imgui is ImGui-dependent.
if(NOT KEEPERFX_IMGUI)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/kfx/imgui/.*\\.cpp$")
    list(APPEND KEEPERFX_SOURCES_CXX "${CMAKE_SOURCE_DIR}/src/kfx/imgui/DevTools.cpp")
endif()
list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/kfx/imgui/vendors/.*\\.cpp$")

# ━━━ Networking Exclusions ━━━
if(NOT KEEPERFX_NETWORKING)
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/api\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_tcpsp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_base_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_client_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_server_tcp\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_enet\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_portforward\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_netsession\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_netsp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_network\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_network_exchange\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_resync\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_input_lag\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_received_packets\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_redundant_packets\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_checksums\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_game\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_enet\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_holepunch\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_lan\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_lobby\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_main\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_exchange_common\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_exchange_gameplay\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_base_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_client_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_server_tcp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/net_portforward\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/net_resync\\.cpp$")
    list(APPEND KEEPERFX_SOURCES_C "src/bflib_network_stub.c")
endif()

# ━━━ Matchmaking Exclusion ━━━
if(NOT KEEPERFX_MATCHMAKING)
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/net_matchmaking\\.c$")
endif()

if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/audio/audio_openal\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/input/input_sdl\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_cpu\\.c$")
    list(APPEND KEEPERFX_SOURCES_C "src/bflib_cpu_stub.c")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/bflib_fmvids\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/KeeperSpeechImp\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/steam_api\\.c$")
    
    if(NOT PLATFORM_VITA)
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/custom_sprites\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/custom_sprites_cache\\.c$")
    endif()
    
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/moonphase\\.c$")
    
    if(NOT PLATFORM_VITA)
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_api.*\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_base\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_cfg_funcs\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_params\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_triggers\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/lua_utils\\.c$")
        list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/console_cmd\\.c$")
    endif()
    
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/scrcapt\\.c$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_sndlib\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/bflib_fmvids\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/cdrom\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/linux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/steam_api\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/windows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/WindowSystemSDL\\.cpp$")
    
    # Platform-specific inclusions
    if(PLATFORM_VITA)
        file(GLOB VITA_RENDERER_SOURCES "${CMAKE_SOURCE_DIR}/src/renderer/vita/*.cpp")
        list(APPEND KEEPERFX_SOURCES_CXX
            "src/platform/PlatformVita.cpp"
            "src/platform/WindowSystemVita.cpp"
            "src/platform/PlatformHomebrewMain.cpp"
            "src/renderer/RendererVita.cpp"
            "src/renderer/backends/VitaGPUBackend.cpp"
            ${VITA_RENDERER_SOURCES})
    elseif(PLATFORM_3DS)
        list(APPEND KEEPERFX_SOURCES_CXX
            "src/platform/Platform3DS.cpp"
            "src/platform/PlatformHomebrewMain.cpp"
            "src/renderer/Renderer3DS.cpp")
    elseif(PLATFORM_SWITCH)
        list(APPEND KEEPERFX_SOURCES_CXX
            "src/platform/PlatformSwitch.cpp"
            "src/platform/PlatformHomebrewMain.cpp")
    endif()
    
    list(APPEND KEEPERFX_SOURCES_C "src/platform_shims.c")
endif()

# ━━━ Vita-specific additions ━━━
if(PLATFORM_VITA)
    list(APPEND KEEPERFX_SOURCES_CXX "src/bflib_fmvids.cpp")
    list(APPEND KEEPERFX_SOURCES_C "src/platform/vita_sce_diag.c")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/debugscreen/debugScreenFont\\.c$")
    list(FILTER KEEPERFX_SOURCES_C EXCLUDE REGEX ".*/debugscreen/debugScreen\\.c$")
    list(APPEND KEEPERFX_SOURCES_C "src/platform/debugscreen/debugScreen.c")
    set_source_files_properties("src/platform/debugscreen/debugScreen.c"
        PROPERTIES COMPILE_FLAGS "-Wno-shadow")
endif()

# ━━━ Desktop platform filtering (Windows vs Linux) ━━━
if(WIN32 OR MINGW)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/linux\\.cpp$")
elseif(UNIX AND NOT APPLE)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/windows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/cdrom\\.cpp$")
elseif(APPLE)
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformLinux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/platform/PlatformWindows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/linux\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/src/windows\\.cpp$")
    list(FILTER KEEPERFX_SOURCES_CXX EXCLUDE REGEX ".*/cdrom\\.cpp$")
endif()

# ━━━ Add Library for centijson (homebrew only) ━━━
if(PLATFORM_VITA OR PLATFORM_3DS OR PLATFORM_SWITCH)
    add_library(centijson_static STATIC
        deps/centijson/json.c
        deps/centijson/json-dom.c
        deps/centijson/json-ptr.c
        deps/centijson/value.c)
    target_include_directories(centijson_static PUBLIC deps/centijson/include)
    
    add_library(centitoml OBJECT deps/centitoml/toml_api.c)
    target_include_directories(centitoml INTERFACE deps/centitoml)
    target_link_libraries(centitoml PUBLIC centijson_static)
endif()

# ━━━ Main Target: keeperfx ━━━
add_executable(keeperfx ${KEEPERFX_SOURCES_C} ${KEEPERFX_SOURCES_CXX})
if(MSVC)
    set_target_properties(keeperfx PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/.deploy"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/.deploy"
    )
endif()
target_include_directories(keeperfx PRIVATE src deps/centijson/include)
if(KEEPERFX_HVLOG)
    target_compile_definitions(keeperfx PUBLIC BFDEBUG_LEVEL=10)
else()
    target_compile_definitions(keeperfx PUBLIC BFDEBUG_LEVEL=0)
endif()
if(KEEPERFX_FORCE_ALEX)
    target_compile_definitions(keeperfx PRIVATE KEEPERFX_FORCE_ALEX=1)
endif()
if(WIN32)
    target_sources(keeperfx PRIVATE "res/keeperfx_stdres.rc")
endif()
apply_keeperfx_warnings(keeperfx)
if(PLATFORM_VITA)
    target_compile_options(keeperfx PRIVATE -Wno-format-truncation)
endif()
apply_keeperfx_link_flags(keeperfx)
apply_windows_system_libs(keeperfx)

# Link SDL3 (vcpkg static targets have -static suffix; mingw/Linux use plain names)
macro(kfx_link_sdl3_target TARGET_NAME)
    if(TARGET SDL3::SDL3-static)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3::SDL3-static)
    elseif(TARGET SDL3::SDL3)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3::SDL3 SDL3::SDL3main)
    elseif(TARGET SDL3)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3)
    endif()
    if(TARGET SDL3_image::SDL3_image-static)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3_image::SDL3_image-static)
    elseif(TARGET SDL3_image::SDL3_image)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3_image::SDL3_image)
    endif()
    if(TARGET SDL3_mixer::SDL3_mixer-static)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3_mixer::SDL3_mixer-static)
    elseif(TARGET SDL3_mixer::SDL3_mixer)
        target_link_libraries(${TARGET_NAME} PRIVATE SDL3_mixer::SDL3_mixer)
    endif()
    # SDL_net removed: api.c now uses native Winsock/BSD sockets
endmacro()
kfx_link_sdl3_target(keeperfx)

# Link glad for OpenGL renderer (MSVC/vcpkg path; MinGW deferred to keeperfx-deps)
if(KEEPERFX_RENDERER_OPENGL AND TARGET glad::glad)
    target_link_libraries(keeperfx PRIVATE glad::glad)
endif()

# Link Tracy profiler (FetchContent target — compiled from source, matches CRT)
if(KEEPERFX_TRACY AND TARGET TracyClient)
    target_link_libraries(keeperfx PRIVATE TracyClient)
endif()

# Link ImGui (optional — desktop only, requires OpenGL)
if(KEEPERFX_IMGUI AND TARGET imgui::imgui)
    target_link_libraries(keeperfx PRIVATE imgui::imgui)
    target_sources(keeperfx PRIVATE
        "${CMAKE_SOURCE_DIR}/src/kfx/imgui/vendors/imgui_impl_sdl3.cpp"
    )
endif()

# ━━━ All main KeeperFX targets — used by Platform*.cmake modules ━━━
set(KFX_TARGETS keeperfx)

# ━━━ OpenGL shaders are now embedded in the executable ━━━
# Previously copied from src/renderer/opengl/shaders/ but now included as constants
# in src/renderer/opengl/GLShaders.h for easier distribution.

# Remove the crap that comes with pulling in the packages.

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
