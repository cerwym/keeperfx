/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file platform_vk_sdl3.cpp
 *     Vulkan surface management for SDL3-based desktop platforms.
 * @par Purpose:
 *     Implements platform_vk_get_instance_extensions / platform_vk_create_surface /
 *     platform_vk_get_drawable_size for any platform using SDL3 as the windowing
 *     layer (Windows, Linux, macOS via MoltenVK).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform.h"
#include "platform/WindowSystemSDL.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "post_inc.h"

extern "C" SDL_Window* lbWindow;

extern "C" void* platform_get_sdl_window(void)
{
    return lbWindow;
}

int platform_vk_get_instance_extensions(unsigned int* count, const char** names)
{
    // SDL3: SDL_Vulkan_GetInstanceExtensions no longer takes a window parameter.
    // It returns a const char* const* directly with the extension count via pointer.
    if (!count)
        return 0;

    // SDL3 API: const char* const* SDL_Vulkan_GetInstanceExtensions(Uint32* count)
    Uint32 ext_count = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!exts)
    {
        *count = 0;
        return 0;
    }

    if (names == nullptr)
    {
        // Caller is querying the count only.
        *count = ext_count;
        return 1;
    }

    // Copy up to *count entries.
    Uint32 copy_count = (ext_count < *count) ? ext_count : *count;
    for (Uint32 i = 0; i < copy_count; ++i)
        names[i] = exts[i];
    *count = copy_count;
    return 1;
}

int platform_vk_create_surface(VkInstance instance, VkSurfaceKHR* out_surface)
{
    SDL_Window* win = lbWindow;
    if (!win || !out_surface)
        return 0;
    // SDL3: SDL_Vulkan_CreateSurface gains an allocator parameter (nullptr = default).
    return SDL_Vulkan_CreateSurface(win, instance, nullptr, out_surface) ? 1 : 0;
}

void platform_vk_get_drawable_size(int* out_w, int* out_h)
{
    SDL_Window* win = lbWindow;
    if (!win)
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    // SDL3: SDL_Vulkan_GetDrawableSize removed; use SDL_GetWindowSizeInPixels.
    SDL_GetWindowSizeInPixels(win, out_w, out_h);
}
