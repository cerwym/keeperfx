/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DebugOverlay.cpp
 *     ImGui debug overlay shell implementation.
 *
 *     Owns the ImGui context, the thread-safe SDL event queue, and whichever
 *     renderer-specific IImGuiRenderBackend was handed to Initialize() --
 *     this file has no idea which graphics API that backend actually uses.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/DebugOverlay.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "kfx/imgui/IImGuiRenderBackend.h"
#include "globals.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
/******************************************************************************/

// SDL is the shared windowing layer for every renderer backend this project
// has (OpenGL today, same SDL3 window for any future GL-alternative backend)
// -- only the *graphics-API* side (IImGuiRenderBackend) is abstracted here,
// not SDL itself.
static std::mutex s_event_mutex;
static std::vector<SDL_Event> s_event_queue;
static std::vector<SDL_Event> s_event_queue_rt;

static std::atomic<bool> s_visible{false};
static std::atomic<bool> s_want_mouse{false};
static std::atomic<bool> s_want_keyboard{false};
static bool s_initialized = false;
static bool s_frame_active = false; // true between a NewFrame() that actually ran and its matching Render()
static std::unique_ptr<kfx::IImGuiRenderBackend> s_backend;

static bool overlay_backend_ready()
{
    if (!s_initialized || !s_backend) {
        return false;
    }
    return ImGui::GetCurrentContext() != nullptr;
}

int DebugOverlay_Initialize(std::unique_ptr<kfx::IImGuiRenderBackend> backend)
{
    if (s_initialized)
        return 1;
    if (!backend)
    {
        WARNLOG("DebugOverlay init skipped: no render backend supplied");
        return 0;
    }

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    if (!backend->Init())
    {
        WARNLOG("DebugOverlay init failed: render backend Init() returned false");
        ImGui::DestroyContext();
        return 0;
    }

    s_backend = std::move(backend);
    s_initialized = true;
    SYNCLOG("DebugOverlay initialized");
    return 1;
}

void DebugOverlay_Shutdown(void)
{
    if (!s_initialized)
        return;

    s_backend->Shutdown();
    s_backend.reset();
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

    if (!s_visible.load())
    {
        // Overlay closed: drop the queued events without touching the
        // backend's platform binding at all. ImGui_ImplSDL3_NewFrame() (via
        // GLImGuiBackend::NewFrame()) forces the OS cursor visible every
        // call unless a window is actually drawn -- skipping it here is
        // what keeps the game's own cursor policy in charge while the
        // overlay isn't in use, and also means the SDL3 cursor/capture/
        // gamepad calls documented as main-thread-only only ever run on
        // this render thread while a developer is actively looking at the
        // overlay, not on every frame of ordinary play.
        s_event_queue_rt.clear();
        s_want_mouse.store(false);
        s_want_keyboard.store(false);
        s_frame_active = false;
        return;
    }

    for (const SDL_Event& ev : s_event_queue_rt)
        s_backend->ProcessEvent(&ev);
    s_event_queue_rt.clear();

    s_backend->NewFrame();
    ImGui::NewFrame();
    s_frame_active = true;
}

void DebugOverlay_Render(void)
{
    if (!s_frame_active)
        return;
    s_frame_active = false;

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

    ImGui::Begin("Debug Overlay");
    ImGui::TextUnformatted("No panels yet -- facade/overlay scaffold only.");
    ImGui::End();

    ImGui::Render();
    s_backend->RenderDrawData();

    ImGuiIO& io = ImGui::GetIO();
    s_want_mouse.store(io.WantCaptureMouse);
    s_want_keyboard.store(io.WantCaptureKeyboard);
}

void DebugOverlay_QueueEvent(const void* sdl_event)
{
    if (!s_initialized || sdl_event == nullptr)
        return;

    const SDL_Event* ev = static_cast<const SDL_Event*>(sdl_event);
    // These event payloads hold pointers into SDL's own per-poll temporary
    // buffers (composed/dropped text, file paths), not into storage SDL
    // guarantees stays valid past the current PollEvent call. Queuing them
    // by value still copies the SDL_Event struct, but the pointer inside it
    // can already be dangling by the time the render thread processes this
    // queue next frame -- the overlay has no text input or drag-drop yet,
    // so just drop these rather than deep-copying the pointed-to data.
    switch (ev->type)
    {
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE:
    case SDL_EVENT_DROP_POSITION:
        return;
    default:
        break;
    }

    std::lock_guard<std::mutex> lock(s_event_mutex);
    s_event_queue.push_back(*ev);
}

void DebugOverlay_Toggle(void)
{
    if (!s_initialized)
        return; // no backend to draw with -- never let F3 lock game input on a renderer/build where the overlay never came up
    bool expected = s_visible.load();
    while (!s_visible.compare_exchange_weak(expected, !expected)) {}
}

int DebugOverlay_IsVisible(void)
{
    return (s_initialized && s_visible.load()) ? 1 : 0;
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
