/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file KfxConfig.cpp
 *     KeeperFX runtime configuration — typed struct and manager.
 * @par Purpose:
 *     Typed keeperfx.cfg load/save with dirty-field tracking and legacy global
 *     synchronization.
 * @author   KeeperFX Team
 * @date     2026
 */
/******************************************************************************/
#include "../../pre_inc.h"
#include "KfxConfig.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../bflib_dernc.h"
#include "../../bflib_fmvids.h"
#include "../../bflib_fileio.h"
#include "../../bflib_math.h"
#include "../../bflib_sound.h"
#include "../../bflib_video.h"
#include "../../config.h"
#include "../../config_keeperfx.h"
#include "../../engine_camera.h"
#include "../../engine_render.h"
#include "../../frontend.h"
#include "../../gui_draw.h"
#include "../../keeperfx.hpp"
#include "../../kfx_memory.h"
#include "../../power_hand.h"
#include "../../renderer/RendererManager.h"
#include "../../scrcapt.h"
#include "../../sounds.h"
#include "../../vidmode.h"

namespace {

struct KfxConfigFieldDesc {
    KfxField field;
    const char* key;
    bool read_only;
};

const KfxConfigFieldDesc k_field_descs[] = {
    {KfxField_InstPath,               "INSTALL_PATH",                     true },
    {KfxField_Language,               "LANGUAGE",                         false},
    {KfxField_Screenshot,             "SCREENSHOT",                       false},
    {KfxField_Censorship,             "CENSORSHIP",                       false},
    {KfxField_PointerSensitivity,     "POINTER_SENSITIVITY",              false},
    {KfxField_AtmosSounds,            "ATMOSPHERIC_SOUNDS",               false},
    {KfxField_AtmosVolume,            "ATMOS_VOLUME",                     false},
    {KfxField_AtmosFrequency,         "ATMOS_FREQUENCY",                  false},
    {KfxField_AtmosSamples,           "ATMOS_SAMPLES",                    false},
    {KfxField_ResizeMovies,           "RESIZE_MOVIES",                    false},
    {KfxField_GuiBlinkRate,           "GUI_BLINK_RATE",                   false},
    {KfxField_NeutralFlashRate,       "NEUTRAL_FLASH_RATE",               false},
    {KfxField_FreezeOnFocusLost,      "FREEZE_GAME_ON_FOCUS_LOST",        false},
    {KfxField_UnlockCursorOnPause,    "UNLOCK_CURSOR_WHEN_GAME_PAUSED",   false},
    {KfxField_LockCursorInPossession, "LOCK_CURSOR_IN_POSSESSION",        false},
    {KfxField_PauseMusicOnPause,      "PAUSE_MUSIC_WHEN_GAME_PAUSED",     false},
    {KfxField_MuteAudioOnFocusLost,   "MUTE_AUDIO_ON_FOCUS_LOST",         false},
    {KfxField_Startup,                "STARTUP",                          true },
    {KfxField_SkipHeartZoom,          "SKIP_HEART_ZOOM",                  false},
    {KfxField_CursorEdgePanning,      "CURSOR_EDGE_CAMERA_PANNING",       false},
    {KfxField_DeltaTime,              "DELTA_TIME",                       false},
    {KfxField_CreatureStatusSize,     "CREATURE_STATUS_SIZE",             false},
    {KfxField_MaxZoomDistance,        "MAX_ZOOM_DISTANCE",                false},
    {KfxField_DisplayNumber,          "DISPLAY_NUMBER",                   false},
    {KfxField_MusicFromDisk,          "MUSIC_FROM_DISK",                  false},
    {KfxField_HandSize,               "HAND_SIZE",                        false},
    {KfxField_LineBoxSize,            "LINE_BOX_SIZE",                    false},
    {KfxField_CommandChar,            "COMMAND_CHAR",                     false},
    {KfxField_ApiEnabled,             "API_ENABLED",                      false},
    {KfxField_ApiPort,                "API_PORT",                         false},
    {KfxField_ExitOnLuaError,         "EXIT_ON_LUA_ERROR",                false},
    {KfxField_TurnsPerSecond,         "TURNS_PER_SECOND",                 false},
    {KfxField_FleeButtonDefault,      "FLEE_BUTTON_DEFAULT",              false},
    {KfxField_ImprisonButtonDefault,  "IMPRISON_BUTTON_DEFAULT",          false},
    {KfxField_FramesPerSecond,        "FRAMES_PER_SECOND",                false},
    {KfxField_TagModeToggling,        "TAG_MODE_TOGGLING",                false},
    {KfxField_DefaultTagMode,         "DEFAULT_TAG_MODE",                 false},
    {KfxField_Renderer,               "RENDERER",                         false},
    {KfxField_PaletteMode,            "PALETTE_MODE",                     false},
    {KfxField_RendererFullbright,     "RENDERER_FULLBRIGHT",              false},
    {KfxField_RendererAmbient,        "RENDERER_AMBIENT",                 false},
    {KfxField_RendererShadeScale,     "RENDERER_SHADE_SCALE",             false},
    {KfxField_RendererShadeGamma,     "RENDERER_SHADE_GAMMA",             false},
    {KfxField_RendererTileFilter,     "RENDERER_TILE_FILTER",             false},
    {KfxField_ZoomBoxMode,            "ZOOM_BOX_MODE",                    false},
    {KfxField_RendererMenuPause,      "RENDERER_MENU_PAUSE",              false},
    {KfxField_FxSprAnimSelectDir,     "FXSPR_ANIM_SELECT_DIR",            false},
};

enum class BoolTextStyle {
    OnOff,
    TrueFalse,
    OneZero,
};

void copy_string(char* dst, size_t dst_size, const char* src)
{
    if (dst_size == 0) {
        return;
    }
    std::snprintf(dst, dst_size, "%s", (src != nullptr) ? src : "");
}

TbBool prepare_diskpath_local(char* buf, long buflen)
{
    int i = (int)std::strlen(buf) - 1;
    if (i >= buflen) {
        i = buflen - 1;
    }
    if (i < 0) {
        return false;
    }
    while (i > 0) {
        if ((buf[i] != '\\') && (buf[i] != '/') && ((unsigned char)(buf[i]) > 32)) {
            break;
        }
        i--;
    }
    while (i >= 1 && buf[i] == '.' && (buf[i - 1] == '/' || buf[i - 1] == '\\')) {
        i -= 2;
        while (i > 0 && (buf[i] == '/' || buf[i] == '\\')) {
            i--;
        }
    }
    buf[i + 1] = '\0';
    return true;
}

int clamp_int(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(max_value, value));
}

std::string trim_copy(const std::string& text)
{
    size_t first = 0;
    while (first < text.size() && std::isspace((unsigned char)text[first]) != 0) {
        first++;
    }
    size_t last = text.size();
    while (last > first && std::isspace((unsigned char)text[last - 1]) != 0) {
        last--;
    }
    return text.substr(first, last - first);
}

bool starts_with_key(const std::string& trimmed, const char* key)
{
    const size_t key_len = std::strlen(key);
    if (trimmed.size() < key_len) {
        return false;
    }
    if (strncasecmp(trimmed.c_str(), key, key_len) != 0) {
        return false;
    }
    if (trimmed.size() == key_len) {
        return true;
    }
    const char next = trimmed[key_len];
    return next == '=' || next == ' ' || next == '\t';
}

const char* bool_text(bool value, BoolTextStyle style)
{
    switch (style)
    {
    case BoolTextStyle::TrueFalse: return value ? "TRUE" : "FALSE";
    case BoolTextStyle::OneZero:   return value ? "1" : "0";
    case BoolTextStyle::OnOff:
    default:                       return value ? "ON" : "OFF";
    }
}

BoolTextStyle get_bool_style(KfxField field)
{
    switch (field)
    {
    case KfxField_SkipHeartZoom:
    case KfxField_ApiEnabled:
    case KfxField_ExitOnLuaError:
        return BoolTextStyle::TrueFalse;
    case KfxField_RendererMenuPause:
        return BoolTextStyle::OneZero;
    default:
        return BoolTextStyle::OnOff;
    }
}

const char* get_named_text(const struct NamedCommand* commands, int value, const char* fallback)
{
    const char* text = get_conf_parameter_text(commands, value);
    if (text != nullptr && text[0] != '\0') {
        return text;
    }
    return fallback;
}

const char* get_tag_mode_text(int value)
{
    switch (value)
    {
    case 1: return "SINGLE";
    case 2: return "DRAG";
    case 3: return "REMEMBER";
    default: return "SINGLE";
    }
}

const char* get_resize_movies_text(const KfxConfig& cfg)
{
    if (!cfg.resize_movies) {
        return "OFF";
    }
    const char* text = get_conf_parameter_text(vidscale_type, (int)cfg.vid_scale_flags);
    if (text != nullptr && text[0] != '\0') {
        return text;
    }
    return "FIT";
}

bool field_equals(const KfxConfig& lhs, const KfxConfig& rhs, KfxField field)
{
    switch (field)
    {
    case KfxField_InstPath:
        return std::strncmp(lhs.inst_path, rhs.inst_path, sizeof(lhs.inst_path)) == 0;
    case KfxField_Language:               return lhs.lang_id == rhs.lang_id;
    case KfxField_Screenshot:             return lhs.screenshot_format == rhs.screenshot_format;
    case KfxField_Censorship:             return lhs.censorship == rhs.censorship;
    case KfxField_PointerSensitivity:     return lhs.pointer_sensitivity_pct == rhs.pointer_sensitivity_pct;
    case KfxField_AtmosSounds:            return lhs.atmos_sounds == rhs.atmos_sounds;
    case KfxField_AtmosVolume:            return lhs.atmos_volume == rhs.atmos_volume;
    case KfxField_AtmosFrequency:         return lhs.atmos_frequency == rhs.atmos_frequency;
    case KfxField_AtmosSamples:
        return lhs.atmos_start == rhs.atmos_start && lhs.atmos_end == rhs.atmos_end && lhs.atmos_repeat == rhs.atmos_repeat;
    case KfxField_ResizeMovies:           return lhs.resize_movies == rhs.resize_movies && lhs.vid_scale_flags == rhs.vid_scale_flags;
    case KfxField_GuiBlinkRate:           return lhs.gui_blink_rate == rhs.gui_blink_rate;
    case KfxField_NeutralFlashRate:       return lhs.neutral_flash_rate == rhs.neutral_flash_rate;
    case KfxField_FreezeOnFocusLost:      return lhs.freeze_on_focus_lost == rhs.freeze_on_focus_lost;
    case KfxField_UnlockCursorOnPause:    return lhs.unlock_cursor_on_pause == rhs.unlock_cursor_on_pause;
    case KfxField_LockCursorInPossession: return lhs.lock_cursor_in_possession == rhs.lock_cursor_in_possession;
    case KfxField_PauseMusicOnPause:      return lhs.pause_music_on_game_pause == rhs.pause_music_on_game_pause;
    case KfxField_MuteAudioOnFocusLost:   return lhs.mute_audio_on_focus_lost == rhs.mute_audio_on_focus_lost;
    case KfxField_Startup:                return lhs.startup_flags == rhs.startup_flags;
    case KfxField_SkipHeartZoom:          return lhs.skip_heart_zoom == rhs.skip_heart_zoom;
    case KfxField_CursorEdgePanning:      return lhs.cursor_edge_cam_panning == rhs.cursor_edge_cam_panning;
    case KfxField_DeltaTime:              return lhs.delta_time == rhs.delta_time;
    case KfxField_CreatureStatusSize:     return lhs.creature_status_size == rhs.creature_status_size;
    case KfxField_MaxZoomDistance:        return lhs.zoom_distance_pct == rhs.zoom_distance_pct;
    case KfxField_DisplayNumber:          return lhs.display_id == rhs.display_id;
    case KfxField_MusicFromDisk:          return lhs.music_from_disk == rhs.music_from_disk;
    case KfxField_HandSize:               return lhs.hand_size_pct == rhs.hand_size_pct;
    case KfxField_LineBoxSize:            return lhs.line_box_size == rhs.line_box_size;
    case KfxField_CommandChar:            return lhs.cmd_char == rhs.cmd_char;
    case KfxField_ApiEnabled:             return lhs.api_enabled == rhs.api_enabled;
    case KfxField_ApiPort:                return lhs.api_port == rhs.api_port;
    case KfxField_ExitOnLuaError:         return lhs.exit_on_lua_error == rhs.exit_on_lua_error;
    case KfxField_TurnsPerSecond:         return lhs.turns_per_second == rhs.turns_per_second;
    case KfxField_FleeButtonDefault:      return lhs.flee_button_default == rhs.flee_button_default;
    case KfxField_ImprisonButtonDefault:  return lhs.imprison_button_default == rhs.imprison_button_default;
    case KfxField_FramesPerSecond:
        return lhs.fps_draw_main == rhs.fps_draw_main && lhs.fps_draw_secondary == rhs.fps_draw_secondary;
    case KfxField_TagModeToggling:        return lhs.tag_mode_toggling == rhs.tag_mode_toggling;
    case KfxField_DefaultTagMode:         return lhs.default_tag_mode == rhs.default_tag_mode;
    case KfxField_Renderer:               return lhs.renderer_type == rhs.renderer_type;
    case KfxField_PaletteMode:            return lhs.palette_mode == rhs.palette_mode;
    case KfxField_RendererFullbright:     return lhs.shade_fullbright == rhs.shade_fullbright;
    case KfxField_RendererAmbient:        return lhs.shade_ambient == rhs.shade_ambient;
    case KfxField_RendererShadeScale:     return lhs.shade_scale == rhs.shade_scale;
    case KfxField_RendererShadeGamma:     return lhs.shade_gamma == rhs.shade_gamma;
    case KfxField_RendererTileFilter:     return lhs.tile_filter == rhs.tile_filter;
    case KfxField_ZoomBoxMode:            return lhs.zoom_box_mode == rhs.zoom_box_mode;
    case KfxField_RendererMenuPause:      return lhs.menu_pause == rhs.menu_pause;
    case KfxField_FxSprAnimSelectDir:     return lhs.fxspr_anim_select_dir == rhs.fxspr_anim_select_dir;
    case KfxField_COUNT:                  return true;
    }
    return true;
}

bool get_field_value_string(const KfxConfig& cfg, KfxField field, char* buf, size_t buflen)
{
    switch (field)
    {
    case KfxField_InstPath:
        std::snprintf(buf, buflen, "%s", cfg.inst_path);
        return true;
    case KfxField_Language:
        std::snprintf(buf, buflen, "%s", get_named_text(lang_type, cfg.lang_id, "ENG"));
        return true;
    case KfxField_Screenshot:
        std::snprintf(buf, buflen, "%s", get_named_text(scrshot_type, cfg.screenshot_format, "PNG"));
        return true;
    case KfxField_Censorship:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.censorship, get_bool_style(field)));
        return true;
    case KfxField_PointerSensitivity:
        std::snprintf(buf, buflen, "%d", cfg.pointer_sensitivity_pct);
        return true;
    case KfxField_AtmosSounds:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.atmos_sounds, get_bool_style(field)));
        return true;
    case KfxField_AtmosVolume:
        std::snprintf(buf, buflen, "%s", get_named_text(atmos_volume, cfg.atmos_volume, "MEDIUM"));
        return true;
    case KfxField_AtmosFrequency:
        std::snprintf(buf, buflen, "%s", get_named_text(atmos_freq, cfg.atmos_frequency, "MEDIUM"));
        return true;
    case KfxField_AtmosSamples:
        std::snprintf(buf, buflen, "%u %u %u", cfg.atmos_start, cfg.atmos_end, cfg.atmos_repeat);
        return true;
    case KfxField_ResizeMovies:
        std::snprintf(buf, buflen, "%s", get_resize_movies_text(cfg));
        return true;
    case KfxField_GuiBlinkRate:
        std::snprintf(buf, buflen, "%d", cfg.gui_blink_rate);
        return true;
    case KfxField_NeutralFlashRate:
        std::snprintf(buf, buflen, "%d", cfg.neutral_flash_rate);
        return true;
    case KfxField_FreezeOnFocusLost:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.freeze_on_focus_lost, get_bool_style(field)));
        return true;
    case KfxField_UnlockCursorOnPause:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.unlock_cursor_on_pause, get_bool_style(field)));
        return true;
    case KfxField_LockCursorInPossession:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.lock_cursor_in_possession, get_bool_style(field)));
        return true;
    case KfxField_PauseMusicOnPause:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.pause_music_on_game_pause, get_bool_style(field)));
        return true;
    case KfxField_MuteAudioOnFocusLost:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.mute_audio_on_focus_lost, get_bool_style(field)));
        return true;
    case KfxField_Startup:
    {
        int written = 0;
        const int ids[] = {1, 2, 3, 4, 5};
        const unsigned int flags[] = {SFlg_Legal, SFlg_FX, SFlg_Bullfrog, SFlg_EA, SFlg_Intro};
        buf[0] = '\0';
        for (int i = 0; i < 5; i++)
        {
            if ((cfg.startup_flags & flags[i]) == 0) {
                continue;
            }
            const char* name = get_conf_parameter_text(startup_parameters, ids[i]);
            written += std::snprintf(buf + written, buflen - (size_t)written, "%s%s", (written > 0) ? " " : "", name);
            if ((size_t)written >= buflen) {
                break;
            }
        }
        return true;
    }
    case KfxField_SkipHeartZoom:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.skip_heart_zoom, get_bool_style(field)));
        return true;
    case KfxField_CursorEdgePanning:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.cursor_edge_cam_panning, get_bool_style(field)));
        return true;
    case KfxField_DeltaTime:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.delta_time, get_bool_style(field)));
        return true;
    case KfxField_CreatureStatusSize:
        std::snprintf(buf, buflen, "%d", cfg.creature_status_size);
        return true;
    case KfxField_MaxZoomDistance:
        std::snprintf(buf, buflen, "%d", cfg.zoom_distance_pct);
        return true;
    case KfxField_DisplayNumber:
        std::snprintf(buf, buflen, "%d", cfg.display_id);
        return true;
    case KfxField_MusicFromDisk:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.music_from_disk, get_bool_style(field)));
        return true;
    case KfxField_HandSize:
        std::snprintf(buf, buflen, "%d", cfg.hand_size_pct);
        return true;
    case KfxField_LineBoxSize:
        std::snprintf(buf, buflen, "%d", cfg.line_box_size);
        return true;
    case KfxField_CommandChar:
        std::snprintf(buf, buflen, "%c", cfg.cmd_char);
        return true;
    case KfxField_ApiEnabled:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.api_enabled, get_bool_style(field)));
        return true;
    case KfxField_ApiPort:
        std::snprintf(buf, buflen, "%u", cfg.api_port);
        return true;
    case KfxField_ExitOnLuaError:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.exit_on_lua_error, get_bool_style(field)));
        return true;
    case KfxField_TurnsPerSecond:
        std::snprintf(buf, buflen, "%d", cfg.turns_per_second);
        return true;
    case KfxField_FleeButtonDefault:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.flee_button_default, get_bool_style(field)));
        return true;
    case KfxField_ImprisonButtonDefault:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.imprison_button_default, get_bool_style(field)));
        return true;
    case KfxField_FramesPerSecond:
        if (cfg.fps_draw_main < 0) {
            if (cfg.fps_draw_secondary > 0) {
                std::snprintf(buf, buflen, "AUTO %d", cfg.fps_draw_secondary);
            } else {
                std::snprintf(buf, buflen, "AUTO");
            }
        } else if (cfg.fps_draw_secondary > 0) {
            std::snprintf(buf, buflen, "%d %d", cfg.fps_draw_main, cfg.fps_draw_secondary);
        } else {
            std::snprintf(buf, buflen, "%d", cfg.fps_draw_main);
        }
        return true;
    case KfxField_TagModeToggling:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.tag_mode_toggling, get_bool_style(field)));
        return true;
    case KfxField_DefaultTagMode:
        std::snprintf(buf, buflen, "%s", get_tag_mode_text(cfg.default_tag_mode));
        return true;
    case KfxField_Renderer:
        std::snprintf(buf, buflen, "%s", get_named_text(renderer_type_names, cfg.renderer_type, "AUTO"));
        return true;
    case KfxField_PaletteMode:
        std::snprintf(buf, buflen, "%s", get_named_text(renderer_palette_mode_names, cfg.palette_mode, "INDEXED"));
        return true;
    case KfxField_RendererFullbright:
        std::snprintf(buf, buflen, "%.4f", cfg.shade_fullbright);
        return true;
    case KfxField_RendererAmbient:
        std::snprintf(buf, buflen, "%.4f", cfg.shade_ambient);
        return true;
    case KfxField_RendererShadeScale:
        std::snprintf(buf, buflen, "%.4f", cfg.shade_scale);
        return true;
    case KfxField_RendererShadeGamma:
        std::snprintf(buf, buflen, "%.4f", cfg.shade_gamma);
        return true;
    case KfxField_RendererTileFilter:
        std::snprintf(buf, buflen, "%s", (cfg.tile_filter == RENDERER_FILTER_LINEAR) ? "LINEAR" : "NEAREST");
        return true;
    case KfxField_ZoomBoxMode:
        std::snprintf(buf, buflen, "%s", get_named_text(renderer_zoom_box_mode_names, cfg.zoom_box_mode, "OVERHEAD"));
        return true;
    case KfxField_RendererMenuPause:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.menu_pause, get_bool_style(field)));
        return true;
    case KfxField_FxSprAnimSelectDir:
        std::snprintf(buf, buflen, "%s", bool_text(cfg.fxspr_anim_select_dir, get_bool_style(field)));
        return true;
    case KfxField_COUNT:
        return false;
    }
    return false;
}

} // namespace

KfxConfigManager& KfxConfigManager::instance()
{
    static KfxConfigManager s_instance;
    return s_instance;
}

KfxConfigManager::KfxConfigManager()
{
    setDefaults();
    m_loaded = m_current;
}

bool KfxConfigManager::load(const char* path)
{
    syncFromLegacyGlobals();
    if (!parseFile(path)) {
        return false;
    }
    syncToLegacyGlobals();
    m_loaded = m_current;
    clearDirty();
    return true;
}

bool KfxConfigManager::save(const char* path)
{
    syncFromLegacyGlobals();
    clearDirty();
    for (int field = 0; field < KfxField_COUNT; field++)
    {
        if (!field_equals(m_current, m_loaded, (KfxField)field)) {
            markDirty((KfxField)field);
        }
    }
    if (!anyDirty()) {
        return true;
    }
    if (!writeFile(path, true)) {
        return false;
    }
    m_loaded = m_current;
    clearDirty();
    return true;
}

bool KfxConfigManager::saveAll(const char* path)
{
    syncFromLegacyGlobals();
    if (!writeFile(path, false)) {
        return false;
    }
    m_loaded = m_current;
    clearDirty();
    return true;
}

void KfxConfigManager::markDirty(KfxField field)
{
    if ((field >= KfxField_InstPath) && (field < KfxField_COUNT)) {
        m_dirty.set((size_t)field);
    }
}

bool KfxConfigManager::isDirty(KfxField field) const
{
    if ((field < KfxField_InstPath) || (field >= KfxField_COUNT)) {
        return false;
    }
    return m_dirty.test((size_t)field);
}

void KfxConfigManager::clearDirty()
{
    m_dirty.reset();
}

void KfxConfigManager::setDefaults()
{
    m_current = {};
    copy_string(m_current.inst_path, sizeof(m_current.inst_path), "./");
    m_current.lang_id = Lang_English;
    m_current.screenshot_format = 1;
    m_current.renderer_type = RENDERER_AUTO;
    m_current.palette_mode = RENDERER_PALETTE_INDEXED;
    m_current.zoom_box_mode = RENDERER_ZBM_OVERHEAD;
    m_current.shade_fullbright = 0.0f;
    m_current.shade_ambient = 0.0f;
    m_current.shade_scale = 1.0f;
    m_current.shade_gamma = 1.0f;
    m_current.tile_filter = RENDERER_FILTER_NEAREST;
    m_current.menu_pause = true;
    m_current.fxspr_anim_select_dir = false;
    m_current.censorship = false;
    m_current.atmos_sounds = false;
    m_current.freeze_on_focus_lost = false;
    m_current.unlock_cursor_on_pause = false;
    m_current.lock_cursor_in_possession = true;
    m_current.pause_music_on_game_pause = false;
    m_current.mute_audio_on_focus_lost = false;
    m_current.skip_heart_zoom = false;
    m_current.cursor_edge_cam_panning = true;
    m_current.delta_time = true;
    m_current.music_from_disk = true;
    m_current.resize_movies = true;
    m_current.vid_scale_flags = SMK_FullscreenFit;
    m_current.atmos_start = 1014;
    m_current.atmos_end = 1034;
    m_current.atmos_repeat = 1013;
    m_current.atmos_volume = 128;
    m_current.atmos_frequency = 800;
    m_current.gui_blink_rate = 1;
    m_current.neutral_flash_rate = 1;
    m_current.creature_status_size = 16;
    m_current.hand_size_pct = 100;
    m_current.line_box_size = 150;
    m_current.pointer_sensitivity_pct = 0;
    m_current.zoom_distance_pct = 60;
    m_current.display_id = 1;
    m_current.turns_per_second = 20;
    m_current.fps_draw_main = 0;
    m_current.fps_draw_secondary = 0;
    m_current.flee_button_default = false;
    m_current.imprison_button_default = false;
    m_current.tag_mode_toggling = false;
    m_current.default_tag_mode = 1;
    m_current.startup_flags = (SFlg_Legal | SFlg_FX | SFlg_Intro);
    m_current.api_enabled = false;
    m_current.api_port = 5599;
    m_current.exit_on_lua_error = false;
    m_current.cmd_char = '!';
}

bool KfxConfigManager::parseFile(const char* path)
{
    const char* config_textname = "keeperfx.cfg";
    long len = LbFileLengthRnc(path);
    if (len < 2) {
        return false;
    }
    if (len > 65536) {
        WARNMSG("%s file \"%s\" is too large.", config_textname, path);
        return false;
    }
    char* buf = (char*)KfxCalloc((size_t)len + 256, 1);
    if (buf == nullptr) {
        return false;
    }
    len = LbFileLoadAt(path, buf);
    if (len <= 0) {
        KfxFree(buf);
        return false;
    }

    buf[len] = '\0';
    text_line_number = 1;
    int32_t pos = 0;
#define COMMAND_TEXT(cmd_num) get_conf_parameter_text(conf_commands, cmd_num)
    while (pos < len)
    {
        int i = 0;
        int n = 0;
        int k = 0;
        int cmd_num = recognize_conf_command(buf, &pos, len, conf_commands);
        char word_buf[128] = {0};
        switch (cmd_num)
        {
        case 1:
            i = get_conf_parameter_whole(buf, &pos, len, m_current.inst_path, sizeof(m_current.inst_path));
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't read \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            prepare_diskpath_local(m_current.inst_path, sizeof(m_current.inst_path));
            if (m_current.inst_path[0] != '/' && std::strchr(m_current.inst_path, ':') == nullptr)
            {
                char resolved[304];
                std::snprintf(resolved, sizeof(resolved), "%s/%s", keeper_runtime_directory, m_current.inst_path);
                prepare_diskpath_local(resolved, sizeof(resolved));
                copy_string(m_current.inst_path, sizeof(m_current.inst_path), resolved);
            }
            break;
        case 2:
        case 4:
            break;
        case 3:
            i = recognize_conf_parameter(buf, &pos, len, lang_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.lang_id = i;
            break;
        case 5:
            i = recognize_conf_parameter(buf, &pos, len, scrshot_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.screenshot_format = i;
            break;
        case 6:
            for (i = 0; i < 3; i++)
            {
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                    k = LbRegisterVideoModeString(word_buf);
                else
                    k = -1;
                if (k <= 0)
                {
                    CONFWRNLOG("Couldn't recognize video mode %d in \"%s\" command of %s file.", i + 1, COMMAND_TEXT(cmd_num), config_textname);
                    continue;
                }
                switch (i)
                {
                case 0: set_failsafe_vidmode((TbScreenMode)k); break;
                case 1: set_movies_vidmode((TbScreenMode)k); break;
                case 2: set_frontend_vidmode((TbScreenMode)k); break;
                }
            }
            break;
        case 7:
            for (i = 0; i < MAX_GAME_VIDMODE_COUNT; i++)
            {
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                {
                    k = LbRegisterVideoModeString(word_buf);
                    if (k > 0) {
                        set_game_vidmode((uint)i, (TbScreenMode)k);
                    } else {
                        CONFWRNLOG("Couldn't recognize video mode %d in \"%s\" command of %s file.", i + 1, COMMAND_TEXT(cmd_num), config_textname);
                    }
                } else
                {
                    if (i > 0) {
                        set_game_vidmode((uint)i, Lb_SCREEN_MODE_INVALID);
                    } else {
                        CONFWRNLOG("Video modes list empty in \"%s\" command of %s file.", COMMAND_TEXT(cmd_num), config_textname);
                    }
                    break;
                }
            }
            break;
        case 8:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.censorship = (i == 1);
            break;
        case 9:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= 1000)) {
                m_current.pointer_sensitivity_pct = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 10:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.atmos_sounds = (i == 1);
            break;
        case 11:
            i = recognize_conf_parameter(buf, &pos, len, atmos_volume);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.atmos_volume = i;
            break;
        case 12:
            i = recognize_conf_parameter(buf, &pos, len, atmos_freq);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.atmos_frequency = i;
            break;
        case 13:
            for (i = 0; i < 3; i++)
            {
                if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
                    k = std::atoi(word_buf);
                else
                    k = -1;
                if (k <= 0)
                {
                    CONFWRNLOG("Couldn't recognize setting %d in \"%s\" command of %s file.", i + 1, COMMAND_TEXT(cmd_num), config_textname);
                    continue;
                }
                switch (i)
                {
                case 0: m_current.atmos_start = (unsigned short)k; break;
                case 1: m_current.atmos_end = (unsigned short)k; break;
                case 2: m_current.atmos_repeat = (unsigned short)k; break;
                }
            }
            break;
        case 14:
            i = recognize_conf_parameter(buf, &pos, len, vidscale_type);
            if (i < 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            if (i > 0) {
                m_current.resize_movies = true;
                m_current.vid_scale_flags = (unsigned int)i;
            } else {
                m_current.resize_movies = false;
            }
            break;
        case 15:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if (i < 1)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            if (i > 160)
            {
                CONFWRNLOG("Value %d out of range for \"%s\" command of %s file. Set to 160.", i, COMMAND_TEXT(cmd_num), config_textname);
                i = 160;
            }
            m_current.gui_blink_rate = i;
            break;
        case 16:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if (i < 1)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            if (i > 160)
            {
                CONFWRNLOG("Value %d out of range for \"%s\" command of %s file. Set to 160.", i, COMMAND_TEXT(cmd_num), config_textname);
                i = 160;
            }
            m_current.neutral_flash_rate = i;
            break;
        case 17:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.freeze_on_focus_lost = (i == 1);
            break;
        case 18:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.unlock_cursor_on_pause = (i == 1);
            break;
        case 19:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.lock_cursor_in_possession = (i == 1);
            break;
        case 20:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.pause_music_on_game_pause = (i == 1);
            break;
        case 21:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.mute_audio_on_focus_lost = (i == 1);
            break;
        case 22:
            m_current.startup_flags = 0;
            while (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                k = (int)get_id(startup_parameters, word_buf);
                switch (k)
                {
                case 1: set_flag(m_current.startup_flags, SFlg_Legal); n++; break;
                case 2: set_flag(m_current.startup_flags, SFlg_FX); n++; break;
                case 3: set_flag(m_current.startup_flags, SFlg_Bullfrog); n++; break;
                case 4: set_flag(m_current.startup_flags, SFlg_EA); n++; break;
                case 5: set_flag(m_current.startup_flags, SFlg_Intro); n++; break;
                default:
                    CONFWRNLOG("Incorrect value of \"%s\" parameter \"%s\" in %s file.", COMMAND_TEXT(cmd_num), word_buf, config_textname);
                    break;
                }
            }
            break;
        case 23:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.skip_heart_zoom = (i == 1);
            break;
        case 24:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.cursor_edge_cam_panning = (i == 1);
            break;
        case 25:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.delta_time = (i == 1);
            break;
        case 26:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= 32768)) {
                m_current.creature_status_size = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 27:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= 32768)) {
                if (i > 100) { i = 100; }
                m_current.zoom_distance_pct = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 28:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= 32768)) {
                m_current.display_id = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 29:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.music_from_disk = (i == 1);
            break;
        case 30:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= SHRT_MAX)) {
                m_current.hand_size_pct = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 31:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= 32768)) {
                m_current.line_box_size = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 32:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.cmd_char = word_buf[0];
            }
            break;
        case 33:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.api_enabled = (i == 1);
            break;
        case 34:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= UINT16_MAX)) {
                m_current.api_port = (uint16_t)i;
            } else {
                CONFWRNLOG("Invalid API port '%s' in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 35:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.exit_on_lua_error = (i == 1);
            break;
        case 36:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                i = std::atoi(word_buf);
            }
            if ((i >= 0) && (i <= INT32_MAX)) {
                m_current.turns_per_second = i;
            } else {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            }
            break;
        case 37:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.flee_button_default = (i == 1);
            break;
        case 38:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.imprison_button_default = (i == 1);
            break;
        case 39:
            if (get_conf_parameter_whole(buf, &pos, len, word_buf, sizeof(word_buf)) > 0)
            {
                int fps_main = m_current.fps_draw_main;
                int fps_secondary = m_current.fps_draw_secondary;
                i = parse_draw_fps_config_val(word_buf, &fps_main, &fps_secondary);
                if (i <= 0) {
                    CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                } else {
                    m_current.fps_draw_main = fps_main;
                    m_current.fps_draw_secondary = fps_secondary;
                }
            }
            break;
        case 40:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.tag_mode_toggling = (i == 1);
            break;
        case 41:
            i = recognize_conf_parameter(buf, &pos, len, tag_modes);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            } else
            {
                m_current.default_tag_mode = i;
            }
            break;
        case 42:
            i = recognize_conf_parameter(buf, &pos, len, renderer_type_names);
            if (i < 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            } else
            {
                m_current.renderer_type = i;
            }
            break;
        case 43:
            i = recognize_conf_parameter(buf, &pos, len, renderer_palette_mode_names);
            if (i < 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            } else
            {
                m_current.palette_mode = i;
            }
            break;
        case 44:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.shade_fullbright = (float)std::atof(word_buf);
            }
            break;
        case 45:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.shade_ambient = (float)std::atof(word_buf);
            }
            break;
        case 46:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.shade_scale = (float)std::atof(word_buf);
            }
            break;
        case 47:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.shade_gamma = (float)std::atof(word_buf);
            }
            break;
        case 48:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.tile_filter = (strcasecmp(word_buf, "LINEAR") == 0) ? RENDERER_FILTER_LINEAR : RENDERER_FILTER_NEAREST;
            }
            break;
        case 49:
            i = recognize_conf_parameter(buf, &pos, len, renderer_zoom_box_mode_names);
            if (i < 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
            } else
            {
                m_current.zoom_box_mode = i;
            }
            break;
        case 50:
            if (get_conf_parameter_single(buf, &pos, len, word_buf, sizeof(word_buf)) > 0) {
                m_current.menu_pause = (std::atoi(word_buf) != 0);
            }
            break;
        case 52:
            i = recognize_conf_parameter(buf, &pos, len, logicval_type);
            if (i <= 0)
            {
                CONFWRNLOG("Couldn't recognize \"%s\" command parameter in %s file.", COMMAND_TEXT(cmd_num), config_textname);
                break;
            }
            m_current.fxspr_anim_select_dir = (i == 1);
            break;
        case ccr_comment:
        case ccr_endOfFile:
            break;
        default:
            CONFWRNLOG("Unrecognized command in %s file.", config_textname);
            break;
        }
        skip_conf_to_next_line(buf, &pos, len);
    }
#undef COMMAND_TEXT
    KfxFree(buf);
    return true;
}

bool KfxConfigManager::writeFile(const char* path, bool dirty_only)
{
    std::string text;
    long len = LbFileLengthRnc(path);
    if (len > 0)
    {
        char* buf = (char*)KfxCalloc((size_t)len + 1, 1);
        if (buf == nullptr) {
            return false;
        }
        long loaded = LbFileLoadAt(path, buf);
        if (loaded < 0)
        {
            KfxFree(buf);
            return false;
        }
        text.assign(buf, (size_t)loaded);
        KfxFree(buf);
    }

    const std::string newline = (text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    std::array<bool, KfxField_COUNT> written{};
    for (std::string& line : lines)
    {
        const std::string trimmed = trim_copy(line);
        for (const KfxConfigFieldDesc& desc : k_field_descs)
        {
            if (!starts_with_key(trimmed, desc.key)) {
                continue;
            }
            if (desc.read_only) {
                break;
            }
            if (dirty_only && !isDirty(desc.field)) {
                break;
            }
            char value_buf[256];
            if (!get_field_value_string(m_current, desc.field, value_buf, sizeof(value_buf))) {
                break;
            }
            line = std::string(desc.key) + "=" + value_buf;
            written[(size_t)desc.field] = true;
            break;
        }
    }

    for (const KfxConfigFieldDesc& desc : k_field_descs)
    {
        if (desc.read_only) {
            continue;
        }
        if (dirty_only && !isDirty(desc.field)) {
            continue;
        }
        if (written[(size_t)desc.field]) {
            continue;
        }
        char value_buf[256];
        if (!get_field_value_string(m_current, desc.field, value_buf, sizeof(value_buf))) {
            continue;
        }
        lines.push_back(std::string(desc.key) + "=" + value_buf);
        written[(size_t)desc.field] = true;
    }

    std::string output;
    for (size_t i = 0; i < lines.size(); i++)
    {
        output += lines[i];
        output += newline;
    }

    char* out_buf = (char*)KfxCalloc(output.size() + 1, 1);
    if (out_buf == nullptr) {
        return false;
    }
    if (!output.empty()) {
        std::memcpy(out_buf, output.data(), output.size());
    }
    long saved = LbFileSaveAt(path, out_buf, (unsigned long)output.size());
    KfxFree(out_buf);
    return saved == (long)output.size();
}

void KfxConfigManager::syncToLegacyGlobals()
{
    copy_string(install_info.inst_path, sizeof(install_info.inst_path), m_current.inst_path);
    install_info.lang_id = m_current.lang_id;
    screenshot_format = (unsigned char)m_current.screenshot_format;
    cfg_renderer_type = m_current.renderer_type;
    cfg_renderer_menu_pause = m_current.menu_pause ? 1 : 0;
    cfg_fxspr_anim_select_dir = m_current.fxspr_anim_select_dir ? 1 : 0;
    g_renderer_settings.palette_mode = m_current.palette_mode;
    g_renderer_settings.zoom_box_mode = m_current.zoom_box_mode;
    g_renderer_settings.shade_fullbright = m_current.shade_fullbright;
    g_renderer_settings.shade_ambient = m_current.shade_ambient;
    g_renderer_settings.shade_scale = m_current.shade_scale;
    g_renderer_settings.shade_gamma = m_current.shade_gamma;
    g_renderer_settings.tile_filter = m_current.tile_filter;

    features_enabled = 0;
    // This was reported by claude, I think it's horseshit.
    // Always-on features are not user-configurable toggles, so they have no
    // m_current field — but this function rebuilds features_enabled from zero,
    // so they must be re-applied here or they are silently cleared (this is
    // what disabled eye lenses).
    features_enabled |= Ft_EyeLens;
    if (m_current.censorship)               features_enabled |= Ft_Censorship;
    if (m_current.atmos_sounds)             features_enabled |= Ft_Atmossounds;
    if (m_current.resize_movies)            features_enabled |= Ft_Resizemovies;
    if (m_current.freeze_on_focus_lost)     features_enabled |= Ft_FreezeOnLoseFocus;
    if (m_current.unlock_cursor_on_pause)   features_enabled |= Ft_UnlockCursorOnPause;
    if (m_current.lock_cursor_in_possession)features_enabled |= Ft_LockCursorInPossession;
    if (m_current.pause_music_on_game_pause)features_enabled |= Ft_PauseMusicOnGamePause;
    if (m_current.mute_audio_on_focus_lost) features_enabled |= Ft_MuteAudioOnLoseFocus;
    if (m_current.skip_heart_zoom)          features_enabled |= Ft_SkipHeartZoom;
    if (!m_current.cursor_edge_cam_panning) features_enabled |= Ft_DisableCursorCameraPanning;
    if (m_current.delta_time)               features_enabled |= Ft_DeltaTime;
    if (m_current.music_from_disk)          features_enabled |= Ft_NoCdMusic;

    vid_scale_flags = m_current.vid_scale_flags;
    AtmosStart = m_current.atmos_start;
    AtmosEnd = m_current.atmos_end;
    AtmosRepeat = m_current.atmos_repeat;
    atmos_sound_volume = m_current.atmos_volume;
    atmos_sound_frequency = m_current.atmos_frequency;
    gui_blink_rate = m_current.gui_blink_rate;
    neutral_flash_rate = m_current.neutral_flash_rate;
    creature_status_size = m_current.creature_status_size;
    line_box_size = m_current.line_box_size;
    base_mouse_sensitivity = (long)((m_current.pointer_sensitivity_pct * 256) / 100);
    global_hand_scale = m_current.hand_size_pct / 100.0f;
    zoom_distance_setting = (long)LbLerp(4100.0f, (float)CAMERA_ZOOM_MIN, (float)m_current.zoom_distance_pct / 100.0f);
    frontview_zoom_distance_setting = (long)LbLerp(16384.0f, (float)FRONTVIEW_CAMERA_ZOOM_MIN, (float)m_current.zoom_distance_pct / 100.0f);
    display_id = (unsigned short)((m_current.display_id == 0) ? 0 : (m_current.display_id - 1));
    cmd_char = m_current.cmd_char;
    api_enabled = m_current.api_enabled ? 1 : 0;
    api_port = m_current.api_port;
    exit_on_lua_error = m_current.exit_on_lua_error ? true : false;
    if (!start_params.overrides[Clo_GameTurns]) {
        start_params.num_fps = m_current.turns_per_second;
    }
    if (!start_params.overrides[Clo_FramesPerSecond])
    {
        start_params.num_fps_draw_main = m_current.fps_draw_main;
        start_params.num_fps_draw_secondary = m_current.fps_draw_secondary;
    }
    FLEE_BUTTON_DEFAULT = m_current.flee_button_default ? true : false;
    IMPRISON_BUTTON_DEFAULT = m_current.imprison_button_default ? true : false;
    right_click_tag_mode_toggle = m_current.tag_mode_toggling ? true : false;
    default_tag_mode = (unsigned char)m_current.default_tag_mode;
    start_params.startup_flags = (unsigned char)m_current.startup_flags;
}

void KfxConfigManager::syncFromLegacyGlobals()
{
    KfxConfig snapshot = {};
    copy_string(snapshot.inst_path, sizeof(snapshot.inst_path), install_info.inst_path);
    snapshot.lang_id = install_info.lang_id;
    snapshot.screenshot_format = screenshot_format;
    snapshot.renderer_type = cfg_renderer_type;
    snapshot.palette_mode = g_renderer_settings.palette_mode;
    snapshot.zoom_box_mode = g_renderer_settings.zoom_box_mode;
    snapshot.shade_fullbright = g_renderer_settings.shade_fullbright;
    snapshot.shade_ambient = g_renderer_settings.shade_ambient;
    snapshot.shade_scale = g_renderer_settings.shade_scale;
    snapshot.shade_gamma = g_renderer_settings.shade_gamma;
    snapshot.tile_filter = g_renderer_settings.tile_filter;
    snapshot.menu_pause = (cfg_renderer_menu_pause != 0);
    snapshot.fxspr_anim_select_dir = (cfg_fxspr_anim_select_dir != 0);
    snapshot.censorship = ((features_enabled & Ft_Censorship) != 0);
    snapshot.atmos_sounds = ((features_enabled & Ft_Atmossounds) != 0);
    snapshot.freeze_on_focus_lost = ((features_enabled & Ft_FreezeOnLoseFocus) != 0);
    snapshot.unlock_cursor_on_pause = ((features_enabled & Ft_UnlockCursorOnPause) != 0);
    snapshot.lock_cursor_in_possession = ((features_enabled & Ft_LockCursorInPossession) != 0);
    snapshot.pause_music_on_game_pause = ((features_enabled & Ft_PauseMusicOnGamePause) != 0);
    snapshot.mute_audio_on_focus_lost = ((features_enabled & Ft_MuteAudioOnLoseFocus) != 0);
    snapshot.skip_heart_zoom = ((features_enabled & Ft_SkipHeartZoom) != 0);
    snapshot.cursor_edge_cam_panning = ((features_enabled & Ft_DisableCursorCameraPanning) == 0);
    snapshot.delta_time = ((features_enabled & Ft_DeltaTime) != 0);
    snapshot.music_from_disk = ((features_enabled & Ft_NoCdMusic) != 0);
    snapshot.resize_movies = ((features_enabled & Ft_Resizemovies) != 0);
    snapshot.vid_scale_flags = vid_scale_flags;
    snapshot.atmos_start = AtmosStart;
    snapshot.atmos_end = AtmosEnd;
    snapshot.atmos_repeat = AtmosRepeat;
    snapshot.atmos_volume = atmos_sound_volume;
    snapshot.atmos_frequency = atmos_sound_frequency;
    snapshot.gui_blink_rate = gui_blink_rate;
    snapshot.neutral_flash_rate = neutral_flash_rate;
    snapshot.creature_status_size = creature_status_size;
    snapshot.hand_size_pct = clamp_int((int)std::lround(global_hand_scale * 100.0f), 0, SHRT_MAX);
    snapshot.line_box_size = line_box_size;
    snapshot.pointer_sensitivity_pct = clamp_int((int)((base_mouse_sensitivity * 100L + 128) / 256), 0, 1000);
    snapshot.zoom_distance_pct = clamp_int((int)std::lround((4100.0f - (float)zoom_distance_setting) * 100.0f / (4100.0f - (float)CAMERA_ZOOM_MIN)), 0, 100);
    snapshot.display_id = (display_id == 0) ? ((m_loaded.display_id == 0) ? 0 : 1) : (display_id + 1);
    snapshot.turns_per_second = (int)start_params.num_fps;
    snapshot.fps_draw_main = start_params.num_fps_draw_main;
    snapshot.fps_draw_secondary = start_params.num_fps_draw_secondary;
    snapshot.flee_button_default = (FLEE_BUTTON_DEFAULT != 0);
    snapshot.imprison_button_default = (IMPRISON_BUTTON_DEFAULT != 0);
    snapshot.tag_mode_toggling = (right_click_tag_mode_toggle != 0);
    snapshot.default_tag_mode = default_tag_mode;
    snapshot.startup_flags = start_params.startup_flags;
    snapshot.api_enabled = (api_enabled != 0);
    snapshot.api_port = api_port;
    snapshot.exit_on_lua_error = (exit_on_lua_error != 0);
    snapshot.cmd_char = cmd_char;
    m_current = snapshot;
}

extern "C" TbBool kfx_cfg_load(const char* path)
{
    return KfxConfigManager::instance().load(path) ? true : false;
}

extern "C" TbBool kfx_cfg_save(const char* path)
{
    return KfxConfigManager::instance().save(path) ? true : false;
}

extern "C" TbBool kfx_cfg_save_all(const char* path)
{
    return KfxConfigManager::instance().saveAll(path) ? true : false;
}

extern "C" TbBool kfx_cfg_is_dirty(void)
{
    return KfxConfigManager::instance().anyDirty() ? true : false;
}

extern "C" void kfx_cfg_mark_dirty(int field)
{
    if (field >= KfxField_InstPath && field < KfxField_COUNT) {
        KfxConfigManager::instance().markDirty((KfxField)field);
    }
}

#include "../../post_inc.h"
