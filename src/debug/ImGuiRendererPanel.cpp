/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiRendererPanel.cpp
 *     ImGui renderer and window settings panel.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "debug/ImGuiRendererPanel.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "platform/PlatformManager.h"
#include "platform/IWindowSystem.h"
#include "renderer/RendererManager.h"
#include "renderer/RendererSettings.h"

#include <SDL2/SDL.h>
#include <imgui.h>

#include <cstdio>
#include <vector>

namespace {

struct ComboOption {
    const char* label;
    int value;
};

const ComboOption kGlowBlendModes[] = {
    {"Additive", RENDERER_GLOW_ADDITIVE},
    {"Screen", RENDERER_GLOW_SCREEN},
};

const ComboOption kDarknessModes[] = {
    {"Linear", RENDERER_DARKNESS_LINEAR},
    {"Palette LUT", RENDERER_DARKNESS_PALETTE},
    {"Animated Fog", RENDERER_DARKNESS_FOG},
};

const ComboOption kShadowTypes[] = {
    {"Off", RENDERER_SHADOW_OFF},
    {"1 Light", RENDERER_SHADOW_1},
    {"2 Lights", RENDERER_SHADOW_2},
    {"3 Lights", RENDERER_SHADOW_3},
    {"4 Lights", RENDERER_SHADOW_4},
    {"Circle Blob", RENDERER_SHADOW_CIRCLE},
};

const ComboOption kFilterModes[] = {
    {"Nearest", RENDERER_FILTER_NEAREST},
    {"Linear", RENDERER_FILTER_LINEAR},
};

const ComboOption kZoomBoxModes[] = {
    {"Overhead", RENDERER_ZBM_OVERHEAD},
    {"Isometric (OpenGL)", RENDERER_ZBM_ISOMETRIC},
};

const ComboOption kPaletteModes[] = {
    {"Indexed (default)", RENDERER_PALETTE_INDEXED},
    {"Truecolour", RENDERER_PALETTE_TRUECOLOUR},
};

const ComboOption kLightingModes[] = {
    {"Software-accurate (default)", RENDERER_LIGHTING_SOFTWARE},
    {"Modern GPU", RENDERER_LIGHTING_MODERN},
};

const ComboOption kTintModes[] = {
    {"GPU shader (future)", RENDERER_TINT_GPU},
    {"CPU fallback (default)", RENDERER_TINT_CPU},
};

int find_option_index(const ComboOption* options, int count, int value)
{
    for (int i = 0; i < count; ++i)
    {
        if (options[i].value == value) {
            return i;
        }
    }
    return 0;
}

bool DrawComboField(const char* label, int* value, const ComboOption* options, int count)
{
    const int current_index = find_option_index(options, count, *value);
    bool changed = false;
    if (ImGui::BeginCombo(label, options[current_index].label))
    {
        for (int i = 0; i < count; ++i)
        {
            const bool selected = (i == current_index);
            if (ImGui::Selectable(options[i].label, selected))
            {
                *value = options[i].value;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawCheckboxField(const char* label, int* value)
{
    bool enabled = (*value != 0);
    if (!ImGui::Checkbox(label, &enabled)) {
        return false;
    }
    *value = enabled ? 1 : 0;
    return true;
}

void ApplyLivePreviewIfChanged(bool changed)
{
    if (changed) {
        RendererApplySettings(&g_renderer_settings);
    }
}

const char* GetRendererTypeLabel(RendererType type)
{
    switch (type)
    {
    case RENDERER_AUTO:     return "Auto";
    case RENDERER_SOFTWARE: return "Software";
    case RENDERER_OPENGL:   return "OpenGL";
    default:                return "Unknown";
    }
}

const char* GetDisplayNameSafe(int display_idx)
{
    const char* name = (display_idx >= 0) ? SDL_GetDisplayName(display_idx) : nullptr;
    return (name != nullptr) ? name : "(unknown)";
}

void CenterWindowOnDisplay(IWindowSystem* ws, int display_idx)
{
    if (ws == nullptr || display_idx < 0) {
        return;
    }

    int dx = 0, dy = 0, dw = 0, dh = 0;
    if (ws->GetDisplayBounds(display_idx, &dx, &dy, &dw, &dh) != 0) {
        return;
    }

    int ww = 0, wh = 0;
    ws->GetWindowSize(&ww, &wh);
    ws->SetWindowPosition(dx + (dw - ww) / 2, dy + (dh - wh) / 2);
}

void FormatDisplayModeLabel(const SDL_DisplayMode& mode, char* buf, size_t bufsz)
{
    if (mode.refresh_rate > 0) {
        std::snprintf(buf, bufsz, "%d x %d @ %d Hz", mode.w, mode.h, mode.refresh_rate);
    } else {
        std::snprintf(buf, bufsz, "%d x %d", mode.w, mode.h);
    }
}

int FindDisplayModeIndex(const std::vector<SDL_DisplayMode>& modes, const SDL_DisplayMode& current_mode)
{
    for (int i = 0; i < static_cast<int>(modes.size()); ++i)
    {
        const SDL_DisplayMode& mode = modes[i];
        if (mode.w == current_mode.w &&
            mode.h == current_mode.h &&
            mode.refresh_rate == current_mode.refresh_rate) {
            return i;
        }
    }
    for (int i = 0; i < static_cast<int>(modes.size()); ++i)
    {
        const SDL_DisplayMode& mode = modes[i];
        if (mode.w == current_mode.w && mode.h == current_mode.h) {
            return i;
        }
    }
    return 0;
}

void DrawWindowTab()
{
    IPlatform* platform = PlatformManager::Get();
    IWindowSystem* ws = (platform != nullptr) ? platform->GetWindowSystem() : nullptr;
    if (ws == nullptr)
    {
        ImGui::TextDisabled("No active window system.");
        return;
    }

    ImGui::Text("Active Renderer: %s", GetRendererTypeLabel(RendererGetActiveType()));

    const unsigned int flags = ws->GetWindowFlags();
    const bool is_fullscreen_desktop =
        (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
    const bool is_fullscreen_exclusive =
        ((flags & SDL_WINDOW_FULLSCREEN) != 0) && !is_fullscreen_desktop;

    int mode_idx = is_fullscreen_desktop ? 2 : (is_fullscreen_exclusive ? 1 : 0);
    const char* modes[] = {
        "Windowed",
        "Fullscreen (Exclusive)",
        "Borderless Fullscreen",
    };
    if (ImGui::Combo("Window Mode", &mode_idx, modes, IM_ARRAYSIZE(modes)))
    {
        if (mode_idx == 0) {
            ws->SetWindowFullscreen(0);
        } else if (mode_idx == 1) {
            ws->SetWindowFullscreen(SDL_WINDOW_FULLSCREEN);
        } else {
            ws->SetWindowFullscreen(SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
    }

    if (mode_idx == 0)
    {
        int current_w = 0, current_h = 0;
        ws->GetWindowSize(&current_w, &current_h);

        static int s_windowed_w = 0;
        static int s_windowed_h = 0;
        static bool s_windowed_dirty = false;

        if (!s_windowed_dirty)
        {
            s_windowed_w = current_w;
            s_windowed_h = current_h;
        }

        ImGui::InputInt("Width##win", &s_windowed_w, 8);
        ImGui::InputInt("Height##win", &s_windowed_h, 8);

        s_windowed_dirty = (s_windowed_w != current_w || s_windowed_h != current_h);
        if (s_windowed_w <= 0 || s_windowed_h <= 0) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Window size must be positive.");
        } else if (s_windowed_dirty && ImGui::Button("Apply Size")) {
            ws->SetWindowSize(s_windowed_w, s_windowed_h);
            s_windowed_dirty = false;
        }
    }

    if (mode_idx == 1)
    {
        const int display_idx = ws->GetWindowDisplayIndex();
        if (display_idx >= 0)
        {
            std::vector<SDL_DisplayMode> modes_for_display;
            const int num_modes = SDL_GetNumDisplayModes(display_idx);
            modes_for_display.reserve((num_modes > 0) ? num_modes : 0);
            for (int i = 0; i < num_modes; ++i)
            {
                SDL_DisplayMode mode = {};
                if (SDL_GetDisplayMode(display_idx, i, &mode) == 0) {
                    modes_for_display.push_back(mode);
                }
            }

            if (!modes_for_display.empty())
            {
                static int s_last_display = -1;
                static int s_selected_mode = 0;

                if (s_last_display != display_idx)
                {
                    s_last_display = display_idx;
                    SDL_DisplayMode current_mode = {};
                    if (SDL_GetCurrentDisplayMode(display_idx, &current_mode) == 0) {
                        s_selected_mode = FindDisplayModeIndex(modes_for_display, current_mode);
                    } else {
                        s_selected_mode = 0;
                    }
                }

                if (s_selected_mode < 0 || s_selected_mode >= static_cast<int>(modes_for_display.size())) {
                    s_selected_mode = 0;
                }

                char current_label[64];
                FormatDisplayModeLabel(modes_for_display[s_selected_mode], current_label, sizeof(current_label));
                if (ImGui::BeginCombo("Resolution", current_label))
                {
                    for (int i = 0; i < static_cast<int>(modes_for_display.size()); ++i)
                    {
                        char label[64];
                        FormatDisplayModeLabel(modes_for_display[i], label, sizeof(label));
                        if (ImGui::Selectable(label, i == s_selected_mode)) {
                            s_selected_mode = i;
                        }
                        if (i == s_selected_mode) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (ImGui::Button("Apply Resolution"))
                {
                    const SDL_DisplayMode& selected = modes_for_display[s_selected_mode];
                    ws->SetWindowDisplayMode(selected.w, selected.h);
                }
            }
            else
            {
                ImGui::TextDisabled("No fullscreen modes reported for this display.");
            }
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Display / Monitor");
    const int num_displays = SDL_GetNumVideoDisplays();
    const int current_display = ws->GetWindowDisplayIndex();
    const char* current_display_name = GetDisplayNameSafe(current_display);
    if (num_displays > 0 && ImGui::BeginCombo("Monitor", current_display_name))
    {
        for (int i = 0; i < num_displays; ++i)
        {
            const bool selected = (i == current_display);
            if (ImGui::Selectable(GetDisplayNameSafe(i), selected)) {
                CenterWindowOnDisplay(ws, i);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    bool bordered = (flags & SDL_WINDOW_BORDERLESS) == 0;
    if (ImGui::Checkbox("Window Border", &bordered)) {
        ws->SetWindowBordered(bordered ? 1 : 0);
    }

    const int refresh_rate = ws->GetDisplayRefreshRate();
    if (refresh_rate > 0) {
        ImGui::Text("Display Refresh Rate: %d Hz", refresh_rate);
    }
}

void DrawShadingTab()
{
    bool changed = false;
    changed |= ImGui::SliderFloat("Shade Fullbright", &g_renderer_settings.shade_fullbright, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shade Ambient", &g_renderer_settings.shade_ambient, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Shade Scale", &g_renderer_settings.shade_scale, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Shade Gamma", &g_renderer_settings.shade_gamma, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("Glow Intensity", &g_renderer_settings.glow_intensity, 0.0f, 2.0f);
    changed |= DrawComboField("Glow Blend Mode", &g_renderer_settings.glow_blend_mode,
        kGlowBlendModes, IM_ARRAYSIZE(kGlowBlendModes));
    changed |= ImGui::SliderFloat("Transparency 1/4 Alpha", &g_renderer_settings.transpar4_alpha, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Transparency 1/8 Alpha", &g_renderer_settings.transpar8_alpha, 0.0f, 1.0f);
    changed |= DrawComboField("Darkness Mode", &g_renderer_settings.darkness_mode,
        kDarknessModes, IM_ARRAYSIZE(kDarknessModes));
    if (g_renderer_settings.darkness_mode == RENDERER_DARKNESS_FOG)
    {
        changed |= ImGui::SliderFloat("Fog Speed", &g_renderer_settings.fog_speed, 0.0f, 5.0f);
        changed |= ImGui::SliderFloat("Fog Density", &g_renderer_settings.fog_density, 0.0f, 1.0f);
    }
    changed |= ImGui::SliderFloat("Tint Strength", &g_renderer_settings.tint_strength, 0.0f, 1.0f);
    ApplyLivePreviewIfChanged(changed);
}

void DrawShadowTab()
{
    bool changed = false;
    changed |= DrawComboField("Shadow Type", &g_renderer_settings.shadow_type,
        kShadowTypes, IM_ARRAYSIZE(kShadowTypes));
    changed |= ImGui::SliderFloat("Shadow Darkness Scale", &g_renderer_settings.shadow_darkness_scale, 0.0f, 2.0f);
    changed |= ImGui::DragInt("Shadow Max Count", &g_renderer_settings.shadow_max_count, 1.0f, 0, 256);

    float shadow_colour[4] = {
        g_renderer_settings.shadow_colour_r,
        g_renderer_settings.shadow_colour_g,
        g_renderer_settings.shadow_colour_b,
        g_renderer_settings.shadow_colour_a,
    };
    if (ImGui::ColorEdit4("Shadow Colour", shadow_colour))
    {
        g_renderer_settings.shadow_colour_r = shadow_colour[0];
        g_renderer_settings.shadow_colour_g = shadow_colour[1];
        g_renderer_settings.shadow_colour_b = shadow_colour[2];
        g_renderer_settings.shadow_colour_a = shadow_colour[3];
        changed = true;
    }

    changed |= DrawCheckboxField("Shadow Depth Test", &g_renderer_settings.shadow_depth_test);
    changed |= DrawComboField("Shadow Filter", &g_renderer_settings.shadow_filter,
        kFilterModes, IM_ARRAYSIZE(kFilterModes));
    ApplyLivePreviewIfChanged(changed);
}

void DrawFiltersTab()
{
    bool changed = false;
    changed |= DrawComboField("Tile Filter", &g_renderer_settings.tile_filter,
        kFilterModes, IM_ARRAYSIZE(kFilterModes));
    changed |= DrawComboField("UI Sprite Filter", &g_renderer_settings.ui_sprite_filter,
        kFilterModes, IM_ARRAYSIZE(kFilterModes));
    changed |= DrawCheckboxField("Wireframe mode", &g_renderer_settings.wireframe);
    changed |= DrawCheckboxField("Depth buffer viz", &g_renderer_settings.show_depth);
    changed |= DrawComboField("Zoom Box Mode", &g_renderer_settings.zoom_box_mode,
        kZoomBoxModes, IM_ARRAYSIZE(kZoomBoxModes));
    changed |= DrawComboField("Palette Mode", &g_renderer_settings.palette_mode,
        kPaletteModes, IM_ARRAYSIZE(kPaletteModes));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Requires restart to take effect");
    ApplyLivePreviewIfChanged(changed);
}

void DrawLightingTab()
{
    bool changed = false;
    changed |= DrawComboField("Lighting Mode", &g_renderer_settings.lighting_mode,
        kLightingModes, IM_ARRAYSIZE(kLightingModes));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.4f, 1.0f), "Modern requires future renderer update");
    changed |= DrawComboField("Tint Mode", &g_renderer_settings.tint_mode,
        kTintModes, IM_ARRAYSIZE(kTintModes));
    changed |= DrawCheckboxField("Creature Outline", &g_renderer_settings.creature_outline_enable);
    if (g_renderer_settings.creature_outline_enable != 0) {
        changed |= ImGui::SliderFloat("Creature Outline Alpha", &g_renderer_settings.creature_outline_alpha, 0.0f, 1.0f);
    }
    ApplyLivePreviewIfChanged(changed);
}

void DrawDebugTab()
{
    bool changed = false;
    changed |= DrawCheckboxField("Wireframe", &g_renderer_settings.wireframe);
    changed |= DrawCheckboxField("Show Depth", &g_renderer_settings.show_depth);
    changed |= DrawCheckboxField("GUI Hitbox Debug", &g_renderer_settings.debug_gui_hitboxes);
    ApplyLivePreviewIfChanged(changed);
}

} // namespace

extern "C" int ImGuiRendererPanel_Draw(void)
{
    int saved = 0;

    ImGui::Begin("Renderer & Window");

    if (ImGui::BeginTabBar("RendererTabs"))
    {
        if (ImGui::BeginTabItem("Window"))   { DrawWindowTab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Shading"))  { DrawShadingTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Shadows"))  { DrawShadowTab();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Filters"))  { DrawFiltersTab();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Lighting")) { DrawLightingTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Debug"))    { DrawDebugTab();    ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Apply & Save"))
    {
        RendererSettings_Sanitize();
        RendererApplySettings(&g_renderer_settings);
        RendererSettings_Save();
        saved = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults"))
    {
        RendererSettings_Reset();
        RendererApplySettings(&g_renderer_settings);
    }

    ImGui::End();
    return saved;
}

#endif

#include "post_inc.h"
