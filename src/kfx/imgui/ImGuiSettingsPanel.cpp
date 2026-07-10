/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiSettingsPanel.cpp
 *     ImGui keeperfx.cfg settings panel.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/ImGuiSettingsPanel.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "bflib_fmvids.h"
#include "config_keeperfx.h"
#include "kfx/config/KfxConfig.hpp"
#include "renderer/RendererManager.h"
#include "renderer/RendererSettings.h"

#include <climits>

#include <imgui.h>

namespace {

struct ComboOption {
    const char* label;
    int value;
};

const ComboOption kLanguageOptions[] = {
    {"English", Lang_English},
    {"French", Lang_French},
    {"German", Lang_German},
    {"Italian", Lang_Italian},
    {"Spanish", Lang_Spanish},
    {"Swedish", Lang_Swedish},
    {"Polish", Lang_Polish},
    {"Dutch", Lang_Dutch},
    {"Hungarian", Lang_Hungarian},
    {"Korean", Lang_Korean},
    {"Danish", Lang_Danish},
    {"Norwegian", Lang_Norwegian},
    {"Czech", Lang_Czech},
    {"Arabic", Lang_Arabic},
    {"Russian", Lang_Russian},
    {"Japanese", Lang_Japanese},
    {"Chinese (Simplified)", Lang_ChineseInt},
    {"Chinese (Traditional)", Lang_ChineseTra},
    {"Portuguese", Lang_Portuguese},
    {"Hindi", Lang_Hindi},
    {"Bengali", Lang_Bengali},
    {"Javanese", Lang_Javanese},
    {"Latin", Lang_Latin},
    {"Ukrainian", Lang_Ukrainian},
};

const ComboOption kScreenshotOptions[] = {
    {"PNG", 1},
    {"BMP", 2},
};

const ComboOption kRendererOptions[] = {
    {"Auto", RENDERER_AUTO},
    {"Software", RENDERER_SOFTWARE},
    {"OpenGL", RENDERER_OPENGL},
};

const ComboOption kPaletteOptions[] = {
    {"Indexed", RENDERER_PALETTE_INDEXED},
    {"Truecolour", RENDERER_PALETTE_TRUECOLOUR},
};

const ComboOption kZoomBoxOptions[] = {
    {"Overhead", RENDERER_ZBM_OVERHEAD},
    {"Isometric", RENDERER_ZBM_ISOMETRIC},
};

const ComboOption kTileFilterOptions[] = {
    {"Nearest", RENDERER_FILTER_NEAREST},
    {"Linear", RENDERER_FILTER_LINEAR},
};

const ComboOption kAtmosVolumeOptions[] = {
    {"Low", 64},
    {"Medium", 128},
    {"High", 255},
};

const ComboOption kAtmosFrequencyOptions[] = {
    {"Low", 3200},
    {"Medium", 800},
    {"High", 400},
};

const ComboOption kResizeMoviesOptions[] = {
    {"Off", 0},
    {"Fit", SMK_FullscreenFit},
    {"Stretch", SMK_FullscreenStretch},
    {"Crop", SMK_FullscreenCrop},
    {"4:3", SMK_FullscreenFit | SMK_FullscreenStretch},
    {"Pixel Perfect", SMK_FullscreenFit | SMK_FullscreenCrop},
    {"4:3 Pixel Perfect", SMK_FullscreenFit | SMK_FullscreenStretch | SMK_FullscreenCrop},
};

const ComboOption kTagModeOptions[] = {
    {"Single", 1},
    {"Drag", 2},
    {"Remember", 3},
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
    int current_index = find_option_index(options, count, *value);
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

bool DrawResizeMoviesField(KfxConfig& cfg)
{
    int value = cfg.resize_movies ? static_cast<int>(cfg.vid_scale_flags) : 0;
    if (!DrawComboField("Resize Movies", &value, kResizeMoviesOptions, IM_ARRAYSIZE(kResizeMoviesOptions))) {
        return false;
    }

    cfg.resize_movies = (value != 0);
    if (cfg.resize_movies) {
        cfg.vid_scale_flags = static_cast<unsigned int>(value);
    }
    return true;
}

bool DrawUShortDrag(const char* label, unsigned short* value, int min_value, int max_value)
{
    int temp = *value;
    if (!ImGui::DragInt(label, &temp, 1.0f, min_value, max_value)) {
        return false;
    }
    if (temp < min_value) {
        temp = min_value;
    }
    if (temp > max_value) {
        temp = max_value;
    }
    *value = static_cast<unsigned short>(temp);
    return true;
}

bool DrawUIntFlagCheckbox(const char* label, unsigned int* flags, unsigned int bit)
{
    bool enabled = ((*flags & bit) != 0);
    if (!ImGui::Checkbox(label, &enabled)) {
        return false;
    }
    if (enabled) {
        *flags |= bit;
    } else {
        *flags &= ~bit;
    }
    return true;
}

void DrawDisplayTab(KfxConfig& cfg, KfxConfigManager& mgr)
{
    if (DrawComboField("Renderer Type", &cfg.renderer_type, kRendererOptions, IM_ARRAYSIZE(kRendererOptions)))
        mgr.markDirty(KfxField_Renderer);
    if (DrawComboField("Palette Mode", &cfg.palette_mode, kPaletteOptions, IM_ARRAYSIZE(kPaletteOptions)))
        mgr.markDirty(KfxField_PaletteMode);
    if (DrawComboField("Zoom Box Mode", &cfg.zoom_box_mode, kZoomBoxOptions, IM_ARRAYSIZE(kZoomBoxOptions)))
        mgr.markDirty(KfxField_ZoomBoxMode);
    if (ImGui::SliderFloat("Shade Fullbright", &cfg.shade_fullbright, 0.0f, 1.0f))
        mgr.markDirty(KfxField_RendererFullbright);
    if (ImGui::SliderFloat("Shade Ambient", &cfg.shade_ambient, 0.0f, 1.0f))
        mgr.markDirty(KfxField_RendererAmbient);
    if (ImGui::SliderFloat("Shade Scale", &cfg.shade_scale, 0.0f, 4.0f))
        mgr.markDirty(KfxField_RendererShadeScale);
    if (ImGui::SliderFloat("Shade Gamma", &cfg.shade_gamma, 0.0f, 4.0f))
        mgr.markDirty(KfxField_RendererShadeGamma);
    if (DrawComboField("Tile Filter", &cfg.tile_filter, kTileFilterOptions, IM_ARRAYSIZE(kTileFilterOptions)))
        mgr.markDirty(KfxField_RendererTileFilter);
    if (ImGui::Checkbox("Menu Pause", &cfg.menu_pause))
        mgr.markDirty(KfxField_RendererMenuPause);
    if (DrawResizeMoviesField(cfg))
        mgr.markDirty(KfxField_ResizeMovies);
    if (ImGui::DragInt("Display Number", &cfg.display_id, 1.0f, 0, SHRT_MAX))
        mgr.markDirty(KfxField_DisplayNumber);
    if (ImGui::SliderInt("FPS Main", &cfg.fps_draw_main, -1, 240, (cfg.fps_draw_main < 0) ? "AUTO" : "%d"))
        mgr.markDirty(KfxField_FramesPerSecond);
    if (ImGui::SliderInt("FPS Secondary", &cfg.fps_draw_secondary, 0, 240))
        mgr.markDirty(KfxField_FramesPerSecond);
    if (DrawComboField("Screenshot Format", &cfg.screenshot_format, kScreenshotOptions, IM_ARRAYSIZE(kScreenshotOptions)))
        mgr.markDirty(KfxField_Screenshot);
}

void DrawGameplayTab(KfxConfig& cfg, KfxConfigManager& mgr)
{
    if (ImGui::SliderInt("Turns Per Second", &cfg.turns_per_second, 1, 40))
        mgr.markDirty(KfxField_TurnsPerSecond);
    if (ImGui::Checkbox("Flee Button Default", &cfg.flee_button_default))
        mgr.markDirty(KfxField_FleeButtonDefault);
    if (ImGui::Checkbox("Imprison Button Default", &cfg.imprison_button_default))
        mgr.markDirty(KfxField_ImprisonButtonDefault);
    if (ImGui::Checkbox("Tag Mode Toggling", &cfg.tag_mode_toggling))
        mgr.markDirty(KfxField_TagModeToggling);
    if (DrawComboField("Default Tag Mode", &cfg.default_tag_mode, kTagModeOptions, IM_ARRAYSIZE(kTagModeOptions)))
        mgr.markDirty(KfxField_DefaultTagMode);
    if (ImGui::Checkbox("Skip Heart Zoom", &cfg.skip_heart_zoom))
        mgr.markDirty(KfxField_SkipHeartZoom);
    if (ImGui::Checkbox("Delta Time", &cfg.delta_time))
        mgr.markDirty(KfxField_DeltaTime);
    if (ImGui::Checkbox("Censorship", &cfg.censorship))
        mgr.markDirty(KfxField_Censorship);
    if (ImGui::DragInt("GUI Blink Rate", &cfg.gui_blink_rate, 1.0f, 1, SHRT_MAX))
        mgr.markDirty(KfxField_GuiBlinkRate);
    if (ImGui::DragInt("Neutral Flash Rate", &cfg.neutral_flash_rate, 1.0f, 1, SHRT_MAX))
        mgr.markDirty(KfxField_NeutralFlashRate);
    if (ImGui::DragInt("Creature Status Size", &cfg.creature_status_size, 1.0f, 0, 32768))
        mgr.markDirty(KfxField_CreatureStatusSize);
    if (ImGui::SliderInt("Hand Size %", &cfg.hand_size_pct, 50, 200))
        mgr.markDirty(KfxField_HandSize);
    if (ImGui::DragInt("Line Box Size", &cfg.line_box_size, 1.0f, 0, 32768))
        mgr.markDirty(KfxField_LineBoxSize);
    if (ImGui::SliderInt("Max Zoom Distance %", &cfg.zoom_distance_pct, 0, 100))
        mgr.markDirty(KfxField_MaxZoomDistance);

    ImGui::TextUnformatted("Startup");
    ImGui::Separator();
    if (DrawUIntFlagCheckbox("Show Legal Screen", &cfg.startup_flags, SFlg_Legal))
        mgr.markDirty(KfxField_Startup);
    if (DrawUIntFlagCheckbox("Show FX Intro", &cfg.startup_flags, SFlg_FX))
        mgr.markDirty(KfxField_Startup);
    if (DrawUIntFlagCheckbox("Show Bullfrog Intro", &cfg.startup_flags, SFlg_Bullfrog))
        mgr.markDirty(KfxField_Startup);
    if (DrawUIntFlagCheckbox("Show EA Intro", &cfg.startup_flags, SFlg_EA))
        mgr.markDirty(KfxField_Startup);
    if (DrawUIntFlagCheckbox("Show Intro Movie", &cfg.startup_flags, SFlg_Intro))
        mgr.markDirty(KfxField_Startup);
}

void DrawInputTab(KfxConfig& cfg, KfxConfigManager& mgr)
{
    if (ImGui::SliderInt("Pointer Sensitivity %", &cfg.pointer_sensitivity_pct, 0, 1000))
        mgr.markDirty(KfxField_PointerSensitivity);
    if (ImGui::Checkbox("Cursor Edge Cam Panning", &cfg.cursor_edge_cam_panning))
        mgr.markDirty(KfxField_CursorEdgePanning);
    if (ImGui::Checkbox("Freeze on Focus Lost", &cfg.freeze_on_focus_lost))
        mgr.markDirty(KfxField_FreezeOnFocusLost);
    if (ImGui::Checkbox("Unlock Cursor on Pause", &cfg.unlock_cursor_on_pause))
        mgr.markDirty(KfxField_UnlockCursorOnPause);
    if (ImGui::Checkbox("Lock Cursor in Possession", &cfg.lock_cursor_in_possession))
        mgr.markDirty(KfxField_LockCursorInPossession);
    if (ImGui::InputText("Install Path", cfg.inst_path, IM_ARRAYSIZE(cfg.inst_path)))
        mgr.markDirty(KfxField_InstPath);

    char cmd_char_buf[2] = {cfg.cmd_char, '\0'};
    if (ImGui::InputText("Command Char", cmd_char_buf, IM_ARRAYSIZE(cmd_char_buf)))
    {
        if (cmd_char_buf[0] != '\0') {
            cfg.cmd_char = cmd_char_buf[0];
            mgr.markDirty(KfxField_CommandChar);
        }
    }
}

void DrawAudioTab(KfxConfig& cfg, KfxConfigManager& mgr)
{
    if (ImGui::Checkbox("Atmos Sounds", &cfg.atmos_sounds))
        mgr.markDirty(KfxField_AtmosSounds);
    if (DrawComboField("Atmos Volume", &cfg.atmos_volume, kAtmosVolumeOptions, IM_ARRAYSIZE(kAtmosVolumeOptions)))
        mgr.markDirty(KfxField_AtmosVolume);
    if (DrawComboField("Atmos Frequency", &cfg.atmos_frequency, kAtmosFrequencyOptions, IM_ARRAYSIZE(kAtmosFrequencyOptions)))
        mgr.markDirty(KfxField_AtmosFrequency);
    if (DrawUShortDrag("Atmos Start", &cfg.atmos_start, 1, USHRT_MAX))
        mgr.markDirty(KfxField_AtmosSamples);
    if (DrawUShortDrag("Atmos End", &cfg.atmos_end, 1, USHRT_MAX))
        mgr.markDirty(KfxField_AtmosSamples);
    if (DrawUShortDrag("Atmos Repeat", &cfg.atmos_repeat, 1, USHRT_MAX))
        mgr.markDirty(KfxField_AtmosSamples);
    if (ImGui::Checkbox("Pause Music on Pause", &cfg.pause_music_on_game_pause))
        mgr.markDirty(KfxField_PauseMusicOnPause);
    if (ImGui::Checkbox("Mute Audio on Focus Lost", &cfg.mute_audio_on_focus_lost))
        mgr.markDirty(KfxField_MuteAudioOnFocusLost);
    if (ImGui::Checkbox("Music from Disk", &cfg.music_from_disk))
        mgr.markDirty(KfxField_MusicFromDisk);
}

void DrawAdvancedTab(KfxConfig& cfg, KfxConfigManager& mgr)
{
    if (DrawComboField("Language", &cfg.lang_id, kLanguageOptions, IM_ARRAYSIZE(kLanguageOptions)))
        mgr.markDirty(KfxField_Language);
    if (ImGui::Checkbox("API Enabled", &cfg.api_enabled))
        mgr.markDirty(KfxField_ApiEnabled);

    int api_port = cfg.api_port;
    if (ImGui::DragInt("API Port", &api_port, 1.0f, 0, USHRT_MAX))
    {
        cfg.api_port = static_cast<uint16_t>(api_port);
        mgr.markDirty(KfxField_ApiPort);
    }

    if (ImGui::Checkbox("Exit on Lua Error", &cfg.exit_on_lua_error))
        mgr.markDirty(KfxField_ExitOnLuaError);
}

} // namespace

extern "C" int ImGuiSettingsPanel_Draw(void)
{
    int saved = 0;
    KfxConfigManager& mgr = KfxConfigManager::instance();
    KfxConfig& cfg = mgr.current();

    ImGui::Begin("Keeper.cfg Settings");

    if (ImGui::BeginTabBar("SettingsTabs"))
    {
        if (ImGui::BeginTabItem("Display"))  { DrawDisplayTab(cfg, mgr);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Gameplay")) { DrawGameplayTab(cfg, mgr); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Input"))    { DrawInputTab(cfg, mgr);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Audio"))    { DrawAudioTab(cfg, mgr);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Advanced")) { DrawAdvancedTab(cfg, mgr); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    const bool dirty = mgr.anyDirty();
    if (dirty) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Unsaved changes");
    }

    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Save Changes"))
    {
        // Push UI edits to legacy globals before save() calls syncFromLegacyGlobals(),
        // otherwise save() would overwrite m_current with the unedited live state.
        mgr.syncToLegacyGlobals();
        write_keeperfx_cfg();
        saved = 1;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save All"))
    {
        mgr.syncToLegacyGlobals();
        write_keeperfx_cfg_all();
        saved = 1;
    }

    ImGui::End();
    return saved;
}

#endif

#include "post_inc.h"
