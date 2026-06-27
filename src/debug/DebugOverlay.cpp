/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DebugOverlay.cpp
 *     ImGui debug overlay shell implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "debug/DebugOverlay.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "debug/ImGuiRendererPanel.hpp"
#include "debug/ImGuiSettingsPanel.hpp"
#include "globals.h"
#include "vendors/imgui_impl_sdl2.h"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <atomic>
#include <mutex>
#include <vector>

static std::mutex s_event_mutex;
static std::vector<SDL_Event> s_event_queue;
static std::vector<SDL_Event> s_event_queue_rt;

static std::atomic<bool> s_visible{false};
static std::atomic<bool> s_want_mouse{false};
static std::atomic<bool> s_want_keyboard{false};
static bool s_initialized = false;

void DebugOverlay_Initialize(void* sdl_window, void* sdl_gl_context)
{
    if (s_initialized)
        return;
    if (sdl_window == nullptr || sdl_gl_context == nullptr)
        return;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(sdl_window), sdl_gl_context);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    s_initialized = true;
    SYNCLOG("DebugOverlay initialized");
}

void DebugOverlay_Shutdown(void)
{
    if (!s_initialized)
        return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    {
        std::lock_guard<std::mutex> lock(s_event_mutex);
        s_event_queue.clear();
        s_event_queue_rt.clear();
    }

    s_want_mouse.store(false);
    s_want_keyboard.store(false);
    s_visible.store(false);
    s_initialized = false;
}

void DebugOverlay_NewFrame(void)
{
    if (!s_initialized)
        return;

    {
        std::lock_guard<std::mutex> lock(s_event_mutex);
        s_event_queue_rt.swap(s_event_queue);
        s_event_queue.clear();
    }

    for (const SDL_Event& ev : s_event_queue_rt)
        ImGui_ImplSDL2_ProcessEvent(&ev);
    s_event_queue_rt.clear();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void DebugOverlay_Render(void)
{
    if (!s_initialized)
        return;

    if (!s_visible.load())
    {
        s_want_mouse.store(false);
        s_want_keyboard.store(false);
        ImGui::EndFrame();
        return;
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("KeeperFX Debug"))
        {
            if (ImGui::MenuItem("Close Overlay", "F3"))
                s_visible.store(false);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGuiRendererPanel_Draw();
    ImGuiSettingsPanel_Draw();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    s_want_mouse.store(io.WantCaptureMouse);
    s_want_keyboard.store(io.WantCaptureKeyboard);
}

void DebugOverlay_QueueEvent(const void* sdl_event)
{
    if (!s_initialized || sdl_event == nullptr)
        return;

    std::lock_guard<std::mutex> lock(s_event_mutex);
    s_event_queue.push_back(*static_cast<const SDL_Event*>(sdl_event));
}

void DebugOverlay_Toggle(void)
{
    bool expected = s_visible.load();
    while (!s_visible.compare_exchange_weak(expected, !expected)) {}
}

int DebugOverlay_IsVisible(void)
{
    return s_visible.load() ? 1 : 0;
}

int DebugOverlay_WantCaptureMouse(void)
{
    return s_want_mouse.load() ? 1 : 0;
}

int DebugOverlay_WantCaptureKeyboard(void)
{
    return s_want_keyboard.load() ? 1 : 0;
}

#endif

#include "post_inc.h"
