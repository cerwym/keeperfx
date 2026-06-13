/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKShaders.cpp
 *     shaderc runtime GLSL→SPIR-V compilation helper.
 */
/******************************************************************************/
#include "pre_inc.h"

#ifdef RENDERER_VULKAN_ENABLED
#ifdef RENDERER_VK_SHADERC_AVAILABLE

#include "renderer/vulkan/VKShaders.h"
#include "globals.h"

#include "post_inc.h"

/******************************************************************************/

std::vector<uint32_t> VKShaders_Compile(shaderc_compiler_t compiler,
                                         const char* src,
                                         shaderc_shader_kind stage,
                                         const char* debug_name)
{
    shaderc_compile_options_t opts = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(opts, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
    shaderc_compile_options_set_optimization_level(opts, shaderc_optimization_level_performance);

    shaderc_compilation_result_t result = shaderc_compile_into_spv(
        compiler, src, strlen(src), stage, debug_name, "main", opts);

    shaderc_compile_options_release(opts);

    if (shaderc_result_get_compilation_status(result) != shaderc_compilation_status_success)
    {
        ERRORLOG("VKShaders: compile failed for '%s': %s",
                 debug_name, shaderc_result_get_error_message(result));
        shaderc_result_release(result);
        return {};
    }

    size_t size      = shaderc_result_get_length(result);
    const uint32_t* words = reinterpret_cast<const uint32_t*>(shaderc_result_get_bytes(result));
    std::vector<uint32_t> spv(words, words + size / sizeof(uint32_t));
    shaderc_result_release(result);
    return spv;
}

/******************************************************************************/

#endif // RENDERER_VK_SHADERC_AVAILABLE
#endif // RENDERER_VULKAN_ENABLED
