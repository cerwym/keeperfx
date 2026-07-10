/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DebugOverlay.cpp
 *     ImGui debug overlay shell implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/DebugOverlay.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "kfx/imgui/ImGuiRendererPanel.hpp"
#include "kfx/imgui/ImGuiSettingsPanel.hpp"
#include "kfx/imgui/ImGuiSpriteAtlasPanel.hpp"
#include "kfx/imgui/ImGuiCheatPanel.hpp"
#include "globals.h"
#include "vendors/imgui_impl_sdl3.h"

#include <SDL3/SDL.h>
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

static bool overlay_backend_ready()
{
    if (!s_initialized) {
        return false;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    return (io.BackendPlatformUserData != nullptr) && (io.BackendRendererUserData != nullptr);
}

int DebugOverlay_Initialize(void* sdl_window, void* sdl_gl_context)
{
    if (s_initialized)
        return 1;
    if (sdl_window == nullptr || sdl_gl_context == nullptr)
    {
        static bool s_warned_null = false;
        if (!s_warned_null)
        {
            WARNLOG("DebugOverlay init deferred: window=%p gl_context=%p",
                    sdl_window, sdl_gl_context);
            s_warned_null = true;
        }
        return 0;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(static_cast<SDL_Window*>(sdl_window), sdl_gl_context);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    s_initialized = true;
    SYNCLOG("DebugOverlay initialized");
    return 1;
}

void DebugOverlay_Shutdown(void)
{
    if (!s_initialized)
        return;

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
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
    if (!overlay_backend_ready())
        return;

    {
        std::lock_guard<std::mutex> lock(s_event_mutex);
        s_event_queue_rt.swap(s_event_queue);
        s_event_queue.clear();
    }

    for (const SDL_Event& ev : s_event_queue_rt)
        ImGui_ImplSDL3_ProcessEvent(&ev);
    s_event_queue_rt.clear();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void DebugOverlay_Render(void)
{
    if (!overlay_backend_ready())
        return;

    const bool overlay_visible = s_visible.load();
    const bool cheat_visible = (ImGuiCheatPanel_IsVisible() != 0);
    if (!overlay_visible && !cheat_visible)
    {
        s_want_mouse.store(false);
        s_want_keyboard.store(false);
        ImGui::EndFrame();
        return;
    }

    if (overlay_visible && ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("KeeperFX Debug"))
        {
            if (ImGui::MenuItem("Close Overlay", "F3"))
                s_visible.store(false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            bool cheat_open = ImGuiCheatPanel_IsVisible() != 0;
            if (ImGui::MenuItem("Cheat Menu", "F12", cheat_open))
                ImGuiCheatPanel_SetVisible(cheat_open ? 0 : 1);
            bool atlas_open = ImGuiSpriteAtlasPanel_IsVisible() != 0;
            if (ImGui::MenuItem("Sprite Atlas Viewer", nullptr, atlas_open))
                ImGuiSpriteAtlasPanel_SetVisible(atlas_open ? 0 : 1);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    if (overlay_visible)
    {
        ImGuiRendererPanel_Draw();
        ImGuiSettingsPanel_Draw();
        ImGuiSpriteAtlasPanel_Draw();
    }
    ImGuiCheatPanel_Draw();

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
