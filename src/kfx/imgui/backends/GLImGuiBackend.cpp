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

#include <string>

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

    // ImGui_ImplSDL3_InitForOpenGL sets SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH=1
    // process-wide, which changes game behaviour (a click that focuses an
    // unfocused window also reaches the game) even when the overlay is never
    // opened. Restore whatever was set before, rather than editing the
    // vendored file -- this keeps that file an unmodified copy.
    const char* prev_clickthrough_hint = SDL_GetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH);
    const bool had_clickthrough_hint = prev_clickthrough_hint != nullptr;
    const std::string saved_clickthrough_hint = had_clickthrough_hint ? prev_clickthrough_hint : std::string();

    if (!ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(m_window), m_gl_context))
        return false;

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, had_clickthrough_hint ? saved_clickthrough_hint.c_str() : "0");

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
