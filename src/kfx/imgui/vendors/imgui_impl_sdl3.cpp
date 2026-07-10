// dear imgui: Platform Backend for SDL3
// Adapted from imgui_impl_sdl2.cpp for SDL3 API changes.

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_sdl3.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimplicit-int-float-conversion"
#endif

#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

// SDL3: global mouse capture is available on most desktop platforms
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS)
#define SDL3_HAS_CAPTURE_AND_GLOBAL_MOUSE 1
#else
#define SDL3_HAS_CAPTURE_AND_GLOBAL_MOUSE 0
#endif

struct ImGui_ImplSDL3_Data
{
    SDL_Window*     Window;
    SDL_WindowID    WindowID;
    SDL_Renderer*   Renderer;
    Uint64          Time;
    char*           ClipboardTextData;

    Uint32          MouseWindowID;
    int             MouseButtonsDown;
    SDL_Cursor*     MouseCursors[ImGuiMouseCursor_COUNT];
    SDL_Cursor*     MouseLastCursor;
    int             MouseLastLeaveFrame;
    bool            MouseCanUseGlobalState;

    ImVector<SDL_Gamepad*>          Gamepads;
    ImGui_ImplSDL3_GamepadMode      GamepadMode;
    bool                            WantUpdateGamepadsList;

    ImGui_ImplSDL3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

static ImGui_ImplSDL3_Data* ImGui_ImplSDL3_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplSDL3_Data*)ImGui::GetIO().BackendPlatformUserData : nullptr;
}

static const char* ImGui_ImplSDL3_GetClipboardText(ImGuiContext*)
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    const char* text = SDL_GetClipboardText();
    bd->ClipboardTextData = text ? SDL_strdup(text) : nullptr;
    return bd->ClipboardTextData;
}

static void ImGui_ImplSDL3_SetClipboardText(ImGuiContext*, const char* text)
{
    SDL_SetClipboardText(text);
}

// SDL3: IME data uses SDL_SetTextInputArea(window, rect, cursor)
static void ImGui_ImplSDL3_PlatformSetImeData(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data)
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    if (data->WantVisible && bd->Window)
    {
        SDL_Rect r;
        r.x = (int)data->InputPos.x;
        r.y = (int)data->InputPos.y;
        r.w = 1;
        r.h = (int)data->InputLineHeight;
        SDL_SetTextInputArea(bd->Window, &r, 0);
    }
}

// SDL3 key mapping uses scancodes for layout-independence
static ImGuiKey ImGui_ImplSDL3_ScancodeToImGuiKey(SDL_Scancode scancode)
{
    switch (scancode)
    {
        case SDL_SCANCODE_TAB:          return ImGuiKey_Tab;
        case SDL_SCANCODE_LEFT:         return ImGuiKey_LeftArrow;
        case SDL_SCANCODE_RIGHT:        return ImGuiKey_RightArrow;
        case SDL_SCANCODE_UP:           return ImGuiKey_UpArrow;
        case SDL_SCANCODE_DOWN:         return ImGuiKey_DownArrow;
        case SDL_SCANCODE_PAGEUP:       return ImGuiKey_PageUp;
        case SDL_SCANCODE_PAGEDOWN:     return ImGuiKey_PageDown;
        case SDL_SCANCODE_HOME:         return ImGuiKey_Home;
        case SDL_SCANCODE_END:          return ImGuiKey_End;
        case SDL_SCANCODE_INSERT:       return ImGuiKey_Insert;
        case SDL_SCANCODE_DELETE:       return ImGuiKey_Delete;
        case SDL_SCANCODE_BACKSPACE:    return ImGuiKey_Backspace;
        case SDL_SCANCODE_SPACE:        return ImGuiKey_Space;
        case SDL_SCANCODE_RETURN:       return ImGuiKey_Enter;
        case SDL_SCANCODE_ESCAPE:       return ImGuiKey_Escape;
        case SDL_SCANCODE_APOSTROPHE:   return ImGuiKey_Apostrophe;
        case SDL_SCANCODE_COMMA:        return ImGuiKey_Comma;
        case SDL_SCANCODE_MINUS:        return ImGuiKey_Minus;
        case SDL_SCANCODE_PERIOD:       return ImGuiKey_Period;
        case SDL_SCANCODE_SLASH:        return ImGuiKey_Slash;
        case SDL_SCANCODE_SEMICOLON:    return ImGuiKey_Semicolon;
        case SDL_SCANCODE_EQUALS:       return ImGuiKey_Equal;
        case SDL_SCANCODE_LEFTBRACKET:  return ImGuiKey_LeftBracket;
        case SDL_SCANCODE_BACKSLASH:    return ImGuiKey_Backslash;
        case SDL_SCANCODE_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDL_SCANCODE_GRAVE:        return ImGuiKey_GraveAccent;
        case SDL_SCANCODE_CAPSLOCK:     return ImGuiKey_CapsLock;
        case SDL_SCANCODE_SCROLLLOCK:   return ImGuiKey_ScrollLock;
        case SDL_SCANCODE_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDL_SCANCODE_PRINTSCREEN:  return ImGuiKey_PrintScreen;
        case SDL_SCANCODE_PAUSE:        return ImGuiKey_Pause;
        case SDL_SCANCODE_KP_0:         return ImGuiKey_Keypad0;
        case SDL_SCANCODE_KP_1:         return ImGuiKey_Keypad1;
        case SDL_SCANCODE_KP_2:         return ImGuiKey_Keypad2;
        case SDL_SCANCODE_KP_3:         return ImGuiKey_Keypad3;
        case SDL_SCANCODE_KP_4:         return ImGuiKey_Keypad4;
        case SDL_SCANCODE_KP_5:         return ImGuiKey_Keypad5;
        case SDL_SCANCODE_KP_6:         return ImGuiKey_Keypad6;
        case SDL_SCANCODE_KP_7:         return ImGuiKey_Keypad7;
        case SDL_SCANCODE_KP_8:         return ImGuiKey_Keypad8;
        case SDL_SCANCODE_KP_9:         return ImGuiKey_Keypad9;
        case SDL_SCANCODE_KP_PERIOD:    return ImGuiKey_KeypadDecimal;
        case SDL_SCANCODE_KP_DIVIDE:    return ImGuiKey_KeypadDivide;
        case SDL_SCANCODE_KP_MULTIPLY:  return ImGuiKey_KeypadMultiply;
        case SDL_SCANCODE_KP_MINUS:     return ImGuiKey_KeypadSubtract;
        case SDL_SCANCODE_KP_PLUS:      return ImGuiKey_KeypadAdd;
        case SDL_SCANCODE_KP_ENTER:     return ImGuiKey_KeypadEnter;
        case SDL_SCANCODE_KP_EQUALS:    return ImGuiKey_KeypadEqual;
        case SDL_SCANCODE_LCTRL:        return ImGuiKey_LeftCtrl;
        case SDL_SCANCODE_LSHIFT:       return ImGuiKey_LeftShift;
        case SDL_SCANCODE_LALT:         return ImGuiKey_LeftAlt;
        case SDL_SCANCODE_LGUI:         return ImGuiKey_LeftSuper;
        case SDL_SCANCODE_RCTRL:        return ImGuiKey_RightCtrl;
        case SDL_SCANCODE_RSHIFT:       return ImGuiKey_RightShift;
        case SDL_SCANCODE_RALT:         return ImGuiKey_RightAlt;
        case SDL_SCANCODE_RGUI:         return ImGuiKey_RightSuper;
        case SDL_SCANCODE_APPLICATION:  return ImGuiKey_Menu;
        case SDL_SCANCODE_0:            return ImGuiKey_0;
        case SDL_SCANCODE_1:            return ImGuiKey_1;
        case SDL_SCANCODE_2:            return ImGuiKey_2;
        case SDL_SCANCODE_3:            return ImGuiKey_3;
        case SDL_SCANCODE_4:            return ImGuiKey_4;
        case SDL_SCANCODE_5:            return ImGuiKey_5;
        case SDL_SCANCODE_6:            return ImGuiKey_6;
        case SDL_SCANCODE_7:            return ImGuiKey_7;
        case SDL_SCANCODE_8:            return ImGuiKey_8;
        case SDL_SCANCODE_9:            return ImGuiKey_9;
        case SDL_SCANCODE_A:            return ImGuiKey_A;
        case SDL_SCANCODE_B:            return ImGuiKey_B;
        case SDL_SCANCODE_C:            return ImGuiKey_C;
        case SDL_SCANCODE_D:            return ImGuiKey_D;
        case SDL_SCANCODE_E:            return ImGuiKey_E;
        case SDL_SCANCODE_F:            return ImGuiKey_F;
        case SDL_SCANCODE_G:            return ImGuiKey_G;
        case SDL_SCANCODE_H:            return ImGuiKey_H;
        case SDL_SCANCODE_I:            return ImGuiKey_I;
        case SDL_SCANCODE_J:            return ImGuiKey_J;
        case SDL_SCANCODE_K:            return ImGuiKey_K;
        case SDL_SCANCODE_L:            return ImGuiKey_L;
        case SDL_SCANCODE_M:            return ImGuiKey_M;
        case SDL_SCANCODE_N:            return ImGuiKey_N;
        case SDL_SCANCODE_O:            return ImGuiKey_O;
        case SDL_SCANCODE_P:            return ImGuiKey_P;
        case SDL_SCANCODE_Q:            return ImGuiKey_Q;
        case SDL_SCANCODE_R:            return ImGuiKey_R;
        case SDL_SCANCODE_S:            return ImGuiKey_S;
        case SDL_SCANCODE_T:            return ImGuiKey_T;
        case SDL_SCANCODE_U:            return ImGuiKey_U;
        case SDL_SCANCODE_V:            return ImGuiKey_V;
        case SDL_SCANCODE_W:            return ImGuiKey_W;
        case SDL_SCANCODE_X:            return ImGuiKey_X;
        case SDL_SCANCODE_Y:            return ImGuiKey_Y;
        case SDL_SCANCODE_Z:            return ImGuiKey_Z;
        case SDL_SCANCODE_F1:           return ImGuiKey_F1;
        case SDL_SCANCODE_F2:           return ImGuiKey_F2;
        case SDL_SCANCODE_F3:           return ImGuiKey_F3;
        case SDL_SCANCODE_F4:           return ImGuiKey_F4;
        case SDL_SCANCODE_F5:           return ImGuiKey_F5;
        case SDL_SCANCODE_F6:           return ImGuiKey_F6;
        case SDL_SCANCODE_F7:           return ImGuiKey_F7;
        case SDL_SCANCODE_F8:           return ImGuiKey_F8;
        case SDL_SCANCODE_F9:           return ImGuiKey_F9;
        case SDL_SCANCODE_F10:          return ImGuiKey_F10;
        case SDL_SCANCODE_F11:          return ImGuiKey_F11;
        case SDL_SCANCODE_F12:          return ImGuiKey_F12;
        case SDL_SCANCODE_F13:          return ImGuiKey_F13;
        case SDL_SCANCODE_F14:          return ImGuiKey_F14;
        case SDL_SCANCODE_F15:          return ImGuiKey_F15;
        case SDL_SCANCODE_F16:          return ImGuiKey_F16;
        case SDL_SCANCODE_F17:          return ImGuiKey_F17;
        case SDL_SCANCODE_F18:          return ImGuiKey_F18;
        case SDL_SCANCODE_F19:          return ImGuiKey_F19;
        case SDL_SCANCODE_F20:          return ImGuiKey_F20;
        case SDL_SCANCODE_F21:          return ImGuiKey_F21;
        case SDL_SCANCODE_F22:          return ImGuiKey_F22;
        case SDL_SCANCODE_F23:          return ImGuiKey_F23;
        case SDL_SCANCODE_F24:          return ImGuiKey_F24;
        case SDL_SCANCODE_AC_BACK:      return ImGuiKey_AppBack;
        case SDL_SCANCODE_AC_FORWARD:   return ImGuiKey_AppForward;
        default: break;
    }
    return ImGuiKey_None;
}

static void ImGui_ImplSDL3_UpdateKeyModifiers(SDL_Keymod sdl_key_mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,  (sdl_key_mods & SDL_KMOD_CTRL)  != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (sdl_key_mods & SDL_KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt,   (sdl_key_mods & SDL_KMOD_ALT)   != 0);
    io.AddKeyEvent(ImGuiMod_Super, (sdl_key_mods & SDL_KMOD_GUI)   != 0);
}

static ImGuiViewport* ImGui_ImplSDL3_GetViewportForWindowID(SDL_WindowID window_id)
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    return (window_id == bd->WindowID) ? ImGui::GetMainViewport() : nullptr;
}

bool ImGui_ImplSDL3_ProcessEvent(const SDL_Event* event)
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplSDL3_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    switch (event->type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->motion.windowID) == nullptr)
                return false;
            ImVec2 mouse_pos(event->motion.x, event->motion.y);
            io.AddMouseSourceEvent(event->motion.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(mouse_pos.x, mouse_pos.y);
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->wheel.windowID) == nullptr)
                return false;
            float wheel_x = -event->wheel.x;
            float wheel_y = event->wheel.y;
            io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMouseWheelEvent(wheel_x, wheel_y);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->button.windowID) == nullptr)
                return false;
            int mouse_button = -1;
            if (event->button.button == SDL_BUTTON_LEFT)   { mouse_button = 0; }
            if (event->button.button == SDL_BUTTON_RIGHT)  { mouse_button = 1; }
            if (event->button.button == SDL_BUTTON_MIDDLE) { mouse_button = 2; }
            if (event->button.button == SDL_BUTTON_X1)     { mouse_button = 3; }
            if (event->button.button == SDL_BUTTON_X2)     { mouse_button = 4; }
            if (mouse_button == -1)
                break;
            io.AddMouseSourceEvent(event->button.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(mouse_button, (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN));
            bd->MouseButtonsDown = (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                ? (bd->MouseButtonsDown | (1 << mouse_button))
                : (bd->MouseButtonsDown & ~(1 << mouse_button));
            return true;
        }
        case SDL_EVENT_TEXT_INPUT:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->text.windowID) == nullptr)
                return false;
            io.AddInputCharactersUTF8(event->text.text);
            return true;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->key.windowID) == nullptr)
                return false;
            // SDL3: modifiers in event->key.mod, key in event->key.key, scancode in event->key.scancode
            ImGui_ImplSDL3_UpdateKeyModifiers((SDL_Keymod)event->key.mod);
            ImGuiKey key = ImGui_ImplSDL3_ScancodeToImGuiKey(event->key.scancode);
            io.AddKeyEvent(key, (event->type == SDL_EVENT_KEY_DOWN));
            io.SetKeyEventNativeData(key, event->key.key, event->key.scancode, event->key.scancode);
            return true;
        }
        // SDL3: flat window events (no longer nested under SDL_WINDOWEVENT)
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            bd->MouseWindowID = event->window.windowID;
            bd->MouseLastLeaveFrame = 0;
            return true;
        }
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            bd->MouseLastLeaveFrame = ImGui::GetFrameCount() + 1;
            return true;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            io.AddFocusEvent(true);
            return true;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        {
            if (ImGui_ImplSDL3_GetViewportForWindowID(event->window.windowID) == nullptr)
                return false;
            io.AddFocusEvent(false);
            return true;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        {
            bd->WantUpdateGamepadsList = true;
            return true;
        }
    }
    return false;
}

static bool ImGui_ImplSDL3_Init(SDL_Window* window, SDL_Renderer* renderer, void* /*sdl_gl_context*/)
{
    ImGuiIO& io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

    bool mouse_can_use_global_state = false;
#if SDL3_HAS_CAPTURE_AND_GLOBAL_MOUSE
    const char* sdl_backend = SDL_GetCurrentVideoDriver();
    const char* global_mouse_whitelist[] = { "windows", "cocoa", "x11", "DIVE", "VMAN" };
    for (int n = 0; n < IM_ARRAYSIZE(global_mouse_whitelist); n++)
        if (strncmp(sdl_backend, global_mouse_whitelist[n], strlen(global_mouse_whitelist[n])) == 0)
            mouse_can_use_global_state = true;
#endif

    ImGui_ImplSDL3_Data* bd = IM_NEW(ImGui_ImplSDL3_Data)();
    io.BackendPlatformUserData = (void*)bd;
    io.BackendPlatformName = "imgui_impl_sdl3";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

    bd->Window = window;
    bd->WindowID = SDL_GetWindowID(window);
    bd->Renderer = renderer;
    bd->MouseCanUseGlobalState = mouse_can_use_global_state;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = ImGui_ImplSDL3_SetClipboardText;
    platform_io.Platform_GetClipboardTextFn = ImGui_ImplSDL3_GetClipboardText;
    platform_io.Platform_ClipboardUserData = nullptr;
    platform_io.Platform_SetImeDataFn = ImGui_ImplSDL3_PlatformSetImeData;

    bd->GamepadMode = ImGui_ImplSDL3_GamepadMode_AutoFirst;
    bd->WantUpdateGamepadsList = true;

    // Load mouse cursors (SDL3 cursor constants are the same)
    bd->MouseCursors[ImGuiMouseCursor_Arrow]      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    bd->MouseCursors[ImGuiMouseCursor_TextInput]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    bd->MouseCursors[ImGuiMouseCursor_ResizeAll]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNS]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    bd->MouseCursors[ImGuiMouseCursor_ResizeEW]   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    bd->MouseCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    bd->MouseCursors[ImGuiMouseCursor_Hand]       = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    bd->MouseCursors[ImGuiMouseCursor_NotAllowed] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED);

    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandle = (void*)(intptr_t)bd->WindowID;
    main_viewport->PlatformHandleRaw = nullptr;

    // SDL3: HWND via window properties (SDL_SysWMinfo removed)
#if defined(SDL_PLATFORM_WIN32)
    main_viewport->PlatformHandleRaw = (void*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__) && defined(SDL_PLATFORM_MACOS)
    main_viewport->PlatformHandleRaw = (void*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#endif

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");

    return true;
}

bool ImGui_ImplSDL3_InitForOpenGL(SDL_Window* window, void* sdl_gl_context)
{
    return ImGui_ImplSDL3_Init(window, nullptr, sdl_gl_context);
}

bool ImGui_ImplSDL3_InitForVulkan(SDL_Window* window)
{
    return ImGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool ImGui_ImplSDL3_InitForD3D(SDL_Window* window)
{
    return ImGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool ImGui_ImplSDL3_InitForMetal(SDL_Window* window)
{
    return ImGui_ImplSDL3_Init(window, nullptr, nullptr);
}

bool ImGui_ImplSDL3_InitForSDLRenderer(SDL_Window* window, SDL_Renderer* renderer)
{
    return ImGui_ImplSDL3_Init(window, renderer, nullptr);
}

bool ImGui_ImplSDL3_InitForOther(SDL_Window* window)
{
    return ImGui_ImplSDL3_Init(window, nullptr, nullptr);
}

static void ImGui_ImplSDL3_CloseGamepads();

void ImGui_ImplSDL3_Shutdown()
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "No platform backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    if (bd->ClipboardTextData)
        SDL_free(bd->ClipboardTextData);
    for (ImGuiMouseCursor cursor_n = 0; cursor_n < ImGuiMouseCursor_COUNT; cursor_n++)
        SDL_DestroyCursor(bd->MouseCursors[cursor_n]);  // SDL3: SDL_DestroyCursor (was SDL_FreeCursor)
    ImGui_ImplSDL3_CloseGamepads();

    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos | ImGuiBackendFlags_HasGamepad);
    IM_DELETE(bd);
}

static void ImGui_ImplSDL3_UpdateMouseData()
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    ImGuiIO& io = ImGui::GetIO();

#if SDL3_HAS_CAPTURE_AND_GLOBAL_MOUSE
    // SDL3: SDL_CaptureMouse takes bool (not SDL_bool)
    SDL_CaptureMouse(bd->MouseButtonsDown != 0);
    SDL_Window* focused_window = SDL_GetKeyboardFocus();
    const bool is_app_focused = (bd->Window == focused_window);
#else
    const bool is_app_focused = (SDL_GetWindowFlags(bd->Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
#endif

    if (is_app_focused)
    {
        if (io.WantSetMousePos)
        {
            // SDL3: SDL_WarpMouseInWindow takes float coordinates
            SDL_WarpMouseInWindow(bd->Window, io.MousePos.x, io.MousePos.y);
        }

        if (bd->MouseCanUseGlobalState && bd->MouseButtonsDown == 0)
        {
            float mouse_x_global, mouse_y_global;
            int window_x, window_y;
            // SDL3: SDL_GetGlobalMouseState takes float*
            SDL_GetGlobalMouseState(&mouse_x_global, &mouse_y_global);
            SDL_GetWindowPosition(bd->Window, &window_x, &window_y);
            io.AddMousePosEvent(mouse_x_global - (float)window_x, mouse_y_global - (float)window_y);
        }
    }
}

static void ImGui_ImplSDL3_UpdateMouseCursor()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
        return;
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None)
    {
        // SDL3: separate SDL_HideCursor() / SDL_ShowCursor()
        SDL_HideCursor();
    }
    else
    {
        SDL_Cursor* expected_cursor = bd->MouseCursors[imgui_cursor] ? bd->MouseCursors[imgui_cursor] : bd->MouseCursors[ImGuiMouseCursor_Arrow];
        if (bd->MouseLastCursor != expected_cursor)
        {
            SDL_SetCursor(expected_cursor);
            bd->MouseLastCursor = expected_cursor;
        }
        SDL_ShowCursor();
    }
}

static void ImGui_ImplSDL3_CloseGamepads()
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    if (bd->GamepadMode != ImGui_ImplSDL3_GamepadMode_Manual)
        for (SDL_Gamepad* gamepad : bd->Gamepads)
            SDL_CloseGamepad(gamepad);
    bd->Gamepads.resize(0);
}

void ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode mode, SDL_Gamepad** manual_gamepads_array, int manual_gamepads_count)
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    ImGui_ImplSDL3_CloseGamepads();
    if (mode == ImGui_ImplSDL3_GamepadMode_Manual)
    {
        IM_ASSERT(manual_gamepads_array != nullptr && manual_gamepads_count > 0);
        for (int n = 0; n < manual_gamepads_count; n++)
            bd->Gamepads.push_back(manual_gamepads_array[n]);
    }
    else
    {
        IM_ASSERT(manual_gamepads_array == nullptr && manual_gamepads_count <= 0);
        bd->WantUpdateGamepadsList = true;
    }
    bd->GamepadMode = mode;
}

static void ImGui_ImplSDL3_UpdateGamepadButton(ImGui_ImplSDL3_Data* bd, ImGuiIO& io, ImGuiKey key, SDL_GamepadButton button_no)
{
    bool merged_value = false;
    for (SDL_Gamepad* gamepad : bd->Gamepads)
        merged_value |= SDL_GetGamepadButton(gamepad, button_no) != 0;
    io.AddKeyEvent(key, merged_value);
}

static inline float Saturate(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

static void ImGui_ImplSDL3_UpdateGamepadAnalog(ImGui_ImplSDL3_Data* bd, ImGuiIO& io, ImGuiKey key, SDL_GamepadAxis axis_no, float v0, float v1)
{
    float merged_value = 0.0f;
    for (SDL_Gamepad* gamepad : bd->Gamepads)
    {
        float vn = Saturate((float)(SDL_GetGamepadAxis(gamepad, axis_no) - v0) / (float)(v1 - v0));
        if (merged_value < vn)
            merged_value = vn;
    }
    io.AddKeyAnalogEvent(key, merged_value > 0.1f, merged_value);
}

static void ImGui_ImplSDL3_UpdateGamepads()
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    ImGuiIO& io = ImGui::GetIO();

    if (bd->WantUpdateGamepadsList && bd->GamepadMode != ImGui_ImplSDL3_GamepadMode_Manual)
    {
        ImGui_ImplSDL3_CloseGamepads();
        // SDL3: enumerate gamepads via SDL_GetGamepads (no more SDL_NumJoysticks + SDL_IsGameController)
        int count = 0;
        SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&count);
        for (int n = 0; n < count; n++)
        {
            SDL_Gamepad* gamepad = SDL_OpenGamepad(gamepad_ids[n]);
            if (gamepad)
            {
                bd->Gamepads.push_back(gamepad);
                if (bd->GamepadMode == ImGui_ImplSDL3_GamepadMode_AutoFirst)
                    break;
            }
        }
        SDL_free(gamepad_ids);
        bd->WantUpdateGamepadsList = false;
    }

    if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0)
        return;
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    if (bd->Gamepads.Size == 0)
        return;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

    // SDL3: SDL_GAMEPAD_BUTTON_* and SDL_GAMEPAD_AXIS_* (renamed from SDL_CONTROLLER_*)
    const int thumb_dead_zone = 8000;
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadStart,       SDL_GAMEPAD_BUTTON_START);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadBack,        SDL_GAMEPAD_BUTTON_BACK);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadFaceLeft,    SDL_GAMEPAD_BUTTON_WEST);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadFaceRight,   SDL_GAMEPAD_BUTTON_EAST);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadFaceUp,      SDL_GAMEPAD_BUTTON_NORTH);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadFaceDown,    SDL_GAMEPAD_BUTTON_SOUTH);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadDpadLeft,    SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadDpadRight,   SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadDpadUp,      SDL_GAMEPAD_BUTTON_DPAD_UP);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadDpadDown,    SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadL1,          SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadR1,          SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadL2,          SDL_GAMEPAD_AXIS_LEFT_TRIGGER,  0.0f, 32767);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadR2,          SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.0f, 32767);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadL3,          SDL_GAMEPAD_BUTTON_LEFT_STICK);
    ImGui_ImplSDL3_UpdateGamepadButton(bd, io, ImGuiKey_GamepadR3,          SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadLStickLeft,  SDL_GAMEPAD_AXIS_LEFTX,  -thumb_dead_zone, -32768);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadLStickRight, SDL_GAMEPAD_AXIS_LEFTX,  +thumb_dead_zone, +32767);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadLStickUp,    SDL_GAMEPAD_AXIS_LEFTY,  -thumb_dead_zone, -32768);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadLStickDown,  SDL_GAMEPAD_AXIS_LEFTY,  +thumb_dead_zone, +32767);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadRStickLeft,  SDL_GAMEPAD_AXIS_RIGHTX, -thumb_dead_zone, -32768);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadRStickRight, SDL_GAMEPAD_AXIS_RIGHTX, +thumb_dead_zone, +32767);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadRStickUp,    SDL_GAMEPAD_AXIS_RIGHTY, -thumb_dead_zone, -32768);
    ImGui_ImplSDL3_UpdateGamepadAnalog(bd, io, ImGuiKey_GamepadRStickDown,  SDL_GAMEPAD_AXIS_RIGHTY, +thumb_dead_zone, +32767);
}

void ImGui_ImplSDL3_NewFrame()
{
    ImGui_ImplSDL3_Data* bd = ImGui_ImplSDL3_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplSDL3_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    int w, h;
    SDL_GetWindowSize(bd->Window, &w, &h);
    if (SDL_GetWindowFlags(bd->Window) & SDL_WINDOW_MINIMIZED)
        w = h = 0;

    // SDL3: SDL_GetWindowSizeInPixels for drawable (framebuffer) size
    int display_w = w, display_h = h;
    SDL_GetWindowSizeInPixels(bd->Window, &display_w, &display_h);

    io.DisplaySize = ImVec2((float)w, (float)h);
    if (w > 0 && h > 0)
        io.DisplayFramebufferScale = ImVec2((float)display_w / w, (float)display_h / h);

    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    if (current_time <= bd->Time)
        current_time = bd->Time + 1;
    io.DeltaTime = bd->Time > 0 ? (float)((double)(current_time - bd->Time) / frequency) : (float)(1.0f / 60.0f);
    bd->Time = current_time;

    if (bd->MouseLastLeaveFrame && bd->MouseLastLeaveFrame >= ImGui::GetFrameCount() && bd->MouseButtonsDown == 0)
    {
        bd->MouseWindowID = 0;
        bd->MouseLastLeaveFrame = 0;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    ImGui_ImplSDL3_UpdateMouseData();
    ImGui_ImplSDL3_UpdateMouseCursor();
    ImGui_ImplSDL3_UpdateGamepads();
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // #ifndef IMGUI_DISABLE
