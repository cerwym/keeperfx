/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file platform_vk_sdl2.cpp
 *     Vulkan surface management for SDL2-based desktop platforms.
 * @par Purpose:
 *     Implements platform_vk_get_instance_extensions / platform_vk_create_surface /
 *     platform_vk_get_drawable_size for any platform using SDL2 as the windowing
 *     layer (Windows, Linux, macOS via MoltenVK).
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform.h"
#include "platform/WindowSystemSDL.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "post_inc.h"

extern "C" SDL_Window* lbWindow;

extern "C" void* platform_get_sdl_window(void)
{
    return lbWindow;
}

int platform_vk_get_instance_extensions(unsigned int* count, const char** names)
{
    SDL_Window* win = lbWindow;
    if (!win)
    {
        // No window yet — SDL can still enumerate extensions from a temporary window.
        // For simplicity, report 0 and let VKDevice::Init() call again after window creation.
        if (count) *count = 0;
        return 1;
    }
    return SDL_Vulkan_GetInstanceExtensions(win, count, names) == SDL_TRUE ? 1 : 0;
}

int platform_vk_create_surface(VkInstance instance, VkSurfaceKHR* out_surface)
{
    SDL_Window* win = lbWindow;
    if (!win || !out_surface)
        return 0;
    return SDL_Vulkan_CreateSurface(win, instance, out_surface) == SDL_TRUE ? 1 : 0;
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
    SDL_Vulkan_GetDrawableSize(win, out_w, out_h);
}
