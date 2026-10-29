/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file GLImGuiBackend.cpp
 *     OpenGL implementation of kfx::IImGuiRenderBackend.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/backends/GLImGuiBackend.h"

#include "kfx/imgui/vendors/imgui_impl_sdl3.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include "post_inc.h"
/******************************************************************************/

namespace kfx {

GLImGuiBackend::GLImGuiBackend(void* sdl_window, void* sdl_gl_context)
    : m_window(sdl_window), m_gl_context(sdl_gl_context)
{
}

bool GLImGuiBackend::Init()
{
    if (m_window == nullptr || m_gl_context == nullptr)
        return false;

    if (!ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(m_window), m_gl_context))
        return false;
    if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
    {
        ImGui_ImplSDL3_Shutdown();
        return false;
    }
    return true;
}

void GLImGuiBackend::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void GLImGuiBackend::NewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}

void GLImGuiBackend::RenderDrawData()
{
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GLImGuiBackend::ProcessEvent(const void* sdl_event)
{
    ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(sdl_event));
}

} // namespace kfx
