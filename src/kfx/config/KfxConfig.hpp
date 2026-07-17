/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file KfxConfig.hpp
 *     KeeperFX runtime configuration — typed struct and manager.
 * @par Purpose:
 *     Single source of truth for all keeperfx.cfg settings, with dirty-field
 *     tracking so only changed values are written back to disk.
 * @author   KeeperFX Team
 * @date     2026
 */
/******************************************************************************/
#ifndef KFX_CONFIG_HPP
#define KFX_CONFIG_HPP

#include "../../bflib_basics.h"

#ifdef __cplusplus
#include <bitset>
#include <cstdint>

enum KfxField {
    KfxField_InstPath = 0,
    KfxField_Language,
    KfxField_Screenshot,
    KfxField_Censorship,
    KfxField_PointerSensitivity,
    KfxField_AtmosSounds,
    KfxField_AtmosVolume,
    KfxField_AtmosFrequency,
    KfxField_AtmosSamples,
    KfxField_ResizeMovies,
    KfxField_GuiBlinkRate,
    KfxField_NeutralFlashRate,
    KfxField_FreezeOnFocusLost,
    KfxField_UnlockCursorOnPause,
    KfxField_LockCursorInPossession,
    KfxField_PauseMusicOnPause,
    KfxField_MuteAudioOnFocusLost,
    KfxField_Startup,
    KfxField_SkipHeartZoom,
    KfxField_CursorEdgePanning,
    KfxField_DeltaTime,
    KfxField_CreatureStatusSize,
    KfxField_MaxZoomDistance,
    KfxField_DisplayNumber,
    KfxField_MusicFromDisk,
    KfxField_HandSize,
    KfxField_LineBoxSize,
    KfxField_CommandChar,
    KfxField_ApiEnabled,
    KfxField_ApiPort,
    KfxField_ExitOnLuaError,
    KfxField_TurnsPerSecond,
    KfxField_FleeButtonDefault,
    KfxField_ImprisonButtonDefault,
    KfxField_FramesPerSecond,
    KfxField_TagModeToggling,
    KfxField_DefaultTagMode,
    KfxField_Renderer,
    KfxField_PaletteMode,
    KfxField_RendererFullbright,
    KfxField_RendererAmbient,
    KfxField_RendererShadeScale,
    KfxField_RendererShadeGamma,
    KfxField_RendererTileFilter,
    KfxField_ZoomBoxMode,
    KfxField_RendererMenuPause,
    KfxField_FxSprAnimSelectDir,
    KfxField_COUNT
};

struct KfxConfig {
    char         inst_path[150];
    int          lang_id;
    int          screenshot_format;
    int          renderer_type;
    int          palette_mode;
    int          zoom_box_mode;
    float        shade_fullbright;
    float        shade_ambient;
    float        shade_scale;
    float        shade_gamma;
    int          tile_filter;
    bool         menu_pause;
    bool         fxspr_anim_select_dir;
    bool         censorship;
    bool         atmos_sounds;
    bool         freeze_on_focus_lost;
    bool         unlock_cursor_on_pause;
    bool         lock_cursor_in_possession;
    bool         pause_music_on_game_pause;
    bool         mute_audio_on_focus_lost;
    bool         skip_heart_zoom;
    bool         cursor_edge_cam_panning;
    bool         delta_time;
    bool         music_from_disk;
    bool         resize_movies;
    unsigned int vid_scale_flags;
    unsigned short atmos_start;
    unsigned short atmos_end;
    unsigned short atmos_repeat;
    int            atmos_volume;
    int            atmos_frequency;
    int          gui_blink_rate;
    int          neutral_flash_rate;
    int          creature_status_size;
    int          hand_size_pct;
    int          line_box_size;
    int          pointer_sensitivity_pct;
    int          zoom_distance_pct;
    int          display_id;
    int          turns_per_second;
    int          fps_draw_main;
    int          fps_draw_secondary;
    bool         flee_button_default;
    bool         imprison_button_default;
    bool         tag_mode_toggling;
    int          default_tag_mode;
    unsigned int startup_flags;
    bool         api_enabled;
    uint16_t     api_port;
    bool         exit_on_lua_error;
    char         cmd_char;
};

class KfxConfigManager {
public:
    static KfxConfigManager& instance();

    bool load(const char* path);
    bool save(const char* path);
    bool saveAll(const char* path);

    void markDirty(KfxField field);
    bool isDirty(KfxField field) const;
    void clearDirty();
    bool anyDirty() const { return m_dirty.any(); }

    KfxConfig& current() { return m_current; }
    const KfxConfig& loaded() const { return m_loaded; }

    void syncToLegacyGlobals();
    void syncFromLegacyGlobals();

private:
    KfxConfigManager();
    ~KfxConfigManager() = default;
    KfxConfigManager(const KfxConfigManager&) = delete;
    KfxConfigManager& operator=(const KfxConfigManager&) = delete;

    void setDefaults();
    bool parseFile(const char* path);
    bool writeFile(const char* path, bool dirty_only);

    KfxConfig m_current{};
    KfxConfig m_loaded{};
    std::bitset<KfxField_COUNT> m_dirty{};
};

#endif // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

TbBool kfx_cfg_load(const char* path);
TbBool kfx_cfg_save(const char* path);
TbBool kfx_cfg_save_all(const char* path);
TbBool kfx_cfg_is_dirty(void);
void   kfx_cfg_mark_dirty(int field);

#ifdef __cplusplus
}
#endif

#endif // KFX_CONFIG_HPP
