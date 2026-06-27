/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_keeperfx.c
 * @par Purpose:
 *     load the main keeperfx.cfg config file.
 * @par Comment:
 *     None.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_keeperfx.h"
#include "kfx/config/KfxConfig.hpp"

#include <stdarg.h>
#include "globals.h"
#include "bflib_basics.h"
#include "bflib_math.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_video.h"
#include "bflib_keybrd.h"
#include "bflib_datetm.h"
#include "bflib_mouse.h"
#include "bflib_sound.h"
#include "bflib_fmvids.h"
#include "bflib_sprfnt.h"
#include "config_campaigns.h"
#include "engine_render.h"
#include "frontend.h"
#include "front_simple.h"
#include "front_input.h"
#include "gui_draw.h"
#include "scrcapt.h"
#include "sounds.h"
#include "vidmode.h"
#include "moonphase.h"
#include "renderer/RendererManager.h"
#include "platform/PlatformManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/


static const char keeper_config_file[]="keeperfx.cfg";

char cmd_char = '!';
unsigned short AtmosRepeat = 1013;
unsigned short AtmosStart = 1014;
unsigned short AtmosEnd = 1034;
TbBool AssignCpuKeepers = 0;
struct InstallInfo install_info;
char keeper_runtime_directory[152];
short api_enabled = false;
uint16_t api_port = 5599;
unsigned long features_enabled = 0;
TbBool exit_on_lua_error = false;
TbBool FLEE_BUTTON_DEFAULT = false;
TbBool IMPRISON_BUTTON_DEFAULT = false;

/**
 * Language 3-char abbreviations.
 * These are selected from ISO 639-2/B naming standard.
 */
const struct NamedCommand lang_type[] = {
  {"ENG", Lang_English},
  {"FRE", Lang_French},
  {"GER", Lang_German},
  {"ITA", Lang_Italian},
  {"SPA", Lang_Spanish},
  {"SWE", Lang_Swedish},
  {"POL", Lang_Polish},
  {"DUT", Lang_Dutch},
  {"HUN", Lang_Hungarian},
  {"KOR", Lang_Korean},
  {"DAN", Lang_Danish},
  {"NOR", Lang_Norwegian},
  {"CZE", Lang_Czech},
  {"ARA", Lang_Arabic},
  {"RUS", Lang_Russian},
  {"JPN", Lang_Japanese},
  {"CHI", Lang_ChineseInt}, // Simplified Chinese
  {"CHT", Lang_ChineseTra}, // Traditional Chinese (not from ISO 639-2/B)
  {"POR", Lang_Portuguese},
  {"HIN", Lang_Hindi},
  {"BEN", Lang_Bengali},
  {"JAV", Lang_Javanese},
  {"LAT", Lang_Latin}, // Classic Latin
  {"UKR", Lang_Ukrainian},
  {NULL,  Lang_Unset},
  };

const struct NamedCommand scrshot_type[] = {
  {"PNG", 1},
  {"BMP", 2},
  {NULL,  0},
  };

const struct NamedCommand renderer_type_names[] = {
  {"AUTO",     RENDERER_AUTO},
  {"SOFTWARE", RENDERER_SOFTWARE},
  {"OPENGL",   RENDERER_OPENGL},
  {NULL, 0},
  };

const struct NamedCommand renderer_palette_mode_names[] = {
  {"INDEXED",    RENDERER_PALETTE_INDEXED},
  {"TRUECOLOUR", RENDERER_PALETTE_TRUECOLOUR},
  {"TRUECOLOR",  RENDERER_PALETTE_TRUECOLOUR}, // alternate spelling
  {NULL, 0},
  };

const struct NamedCommand renderer_zoom_box_mode_names[] = {
  {"OVERHEAD",   RENDERER_ZBM_OVERHEAD},
  {"ISOMETRIC",  RENDERER_ZBM_ISOMETRIC},
  {"ISO",        RENDERER_ZBM_ISOMETRIC}, // shorthand
  {NULL, 0},
  };

int cfg_renderer_type = RENDERER_AUTO;
TbBool cfg_renderer_menu_pause = 1;

const struct NamedCommand atmos_volume[] = {
  {"LOW",     64},
  {"MEDIUM", 128},
  {"HIGH",   255},
  {NULL,  0},
  };

const struct NamedCommand atmos_freq[] = {
  {"LOW",    3200},
  {"MEDIUM",  800},
  {"HIGH",    400},
  {NULL,  0},
  };

const struct NamedCommand conf_commands[] = {
  {"INSTALL_PATH",         1},
  {"INSTALL_TYPE",         2},
  {"LANGUAGE",             3},
  {"KEYBOARD",             4},
  {"SCREENSHOT",           5},
  {"FRONTEND_RES",         6},
  {"INGAME_RES",           7},
  {"CENSORSHIP",           8},
  {"POINTER_SENSITIVITY",  9},
  {"ATMOSPHERIC_SOUNDS",  10},
  {"ATMOS_VOLUME",        11},
  {"ATMOS_FREQUENCY",     12},
  {"ATMOS_SAMPLES",       13},
  {"RESIZE_MOVIES",       14},
  {"GUI_BLINK_RATE",      15},
  {"NEUTRAL_FLASH_RATE",  16},
  {"FREEZE_GAME_ON_FOCUS_LOST"     , 17},
  {"UNLOCK_CURSOR_WHEN_GAME_PAUSED", 18},
  {"LOCK_CURSOR_IN_POSSESSION"     , 19},
  {"PAUSE_MUSIC_WHEN_GAME_PAUSED"  , 20},
  {"MUTE_AUDIO_ON_FOCUS_LOST"      , 21},
  {"STARTUP"                       , 22},
  {"SKIP_HEART_ZOOM"               , 23},
  {"CURSOR_EDGE_CAMERA_PANNING"    , 24},
  {"DELTA_TIME"                    , 25},
  {"CREATURE_STATUS_SIZE"          , 26},
  {"MAX_ZOOM_DISTANCE"             , 27},
  {"DISPLAY_NUMBER"                , 28},
  {"MUSIC_FROM_DISK"               , 29},
  {"HAND_SIZE"                     , 30},
  {"LINE_BOX_SIZE"                 , 31},
  {"COMMAND_CHAR"                  , 32},
  {"API_ENABLED"                   , 33},
  {"API_PORT"                      , 34},
  {"EXIT_ON_LUA_ERROR"             , 35},
  {"TURNS_PER_SECOND"              , 36},
  {"FLEE_BUTTON_DEFAULT"           , 37},
  {"IMPRISON_BUTTON_DEFAULT"       , 38},
  {"FRAMES_PER_SECOND"             , 39},
  {"TAG_MODE_TOGGLING"             , 40},
  {"DEFAULT_TAG_MODE"              , 41},
  {"RENDERER"                      , 42},
  {"PALETTE_MODE"                  , 43},
  {"RENDERER_FULLBRIGHT"           , 44},
  {"RENDERER_AMBIENT"              , 45},
  {"RENDERER_SHADE_SCALE"          , 46},
  {"RENDERER_SHADE_GAMMA"          , 47},
  {"RENDERER_TILE_FILTER"          , 48},
  {"ZOOM_BOX_MODE"                 , 49},
  {"RENDERER_MENU_PAUSE"           , 50},
  {"ZOOM_TO_MOUSE"                 , 51},
  {NULL,                   0},
  };

  const struct NamedCommand vidscale_type[] = {
  {"OFF",          0}, // No scaling of Smacker Video
  {"DISABLED",     0},
  {"FALSE",        0},
  {"NO",           0},
  {"0",            0},
  {"FIT",          SMK_FullscreenFit}, // Fit to fullscreen, using letterbox and pillarbox as necessary
  {"ON",           SMK_FullscreenFit}, // Duplicate of FIT, for legacy reasons
  {"ENABLED",      SMK_FullscreenFit},
  {"TRUE",         SMK_FullscreenFit},
  {"YES",          SMK_FullscreenFit},
  {"1",            SMK_FullscreenFit},
  {"STRETCH",      SMK_FullscreenStretch}, // Stretch to fullscreen - ignores aspect ratio difference between source and destination
  {"CROP",         SMK_FullscreenCrop}, // Fill fullscreen and crop - no letterbox or pillarbox
  {"4BY3",         SMK_FullscreenFit | SMK_FullscreenStretch}, // [Aspect Ratio correction mode] - stretch 320x200 to 4:3 (i.e. increase height by 1.2)
  {"PIXELPERFECT", SMK_FullscreenFit | SMK_FullscreenCrop}, // integer multiple scale only (FIT)
  {"4BY3PP",       SMK_FullscreenFit | SMK_FullscreenStretch | SMK_FullscreenCrop}, // integer multiple scale only (4BY3)
  {NULL,           0},
  };

  const struct NamedCommand startup_parameters[] = {
  {"LEGAL",                   1},
  {"FX",                      2},
  {"BULLFROG",                3}, // hidden
  {"EA",                      4}, // hidden
  {"INTRO",                   5},
  {NULL,                      0},
  };

  const struct NamedCommand tag_modes[] = {
  {"SINGLE",   1},
  {"DRAG",     2},
  {"PRESET",   3}, //legacy
  {"REMEMBER", 3},
  {NULL,       0},
  };

  const struct NamedCommand zoom_to_mouse_options[] = {
  {"NEVER",    ZoomToMouse_Never},
  {"WHEEL",    ZoomToMouse_Wheel},
  {"ALWAYS",   ZoomToMouse_Always},
  {NULL,       0},
  };

unsigned int vid_scale_flags = SMK_FullscreenFit;


/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/

/**
 * Returns if the censorship is on. This mostly affects blood.
 * Originally, censorship was on for german language.
 */
TbBool censorship_enabled(void)
{
  return ((features_enabled & Ft_Censorship) != 0);
}

/**
 * Returns if Athmospheric sound is on.
 */
TbBool atmos_sounds_enabled(void)
{
  return ((features_enabled & Ft_Atmossounds) != 0);
}

/**
 * Returns if Resize Movie is on.
 */
TbBool resize_movies_enabled(void)
{
  return ((features_enabled & Ft_Resizemovies) != 0);
}

#include "game_legacy.h" // it would be nice to not have to include this
/**
 * Returns if we should freeze the game, if the game window loses focus.
 */
TbBool freeze_game_on_focus_lost(void)
{
    if (network_is_active())
    {
        return false;
    }
  return ((features_enabled & Ft_FreezeOnLoseFocus) != 0);
}

/**
 * Returns if we should unlock the mouse cursor from the window, if the user pauses the game.
 */
TbBool unlock_cursor_when_game_paused(void)
{
  return ((features_enabled & Ft_UnlockCursorOnPause) != 0);
}

/**
 * Returns if we should lock the mouse cursor to the window, when the user enters possession mode (when the cursor is already unlocked).
 */
TbBool lock_cursor_in_possession(void)
{
  return ((features_enabled & Ft_LockCursorInPossession) != 0);
}

/**
 * Returns if we should pause the music, if the user pauses the game.
 */
TbBool pause_music_when_game_paused(void)
{
  return ((features_enabled & Ft_PauseMusicOnGamePause) != 0);
}

/**
 * Returns if we should mute the game audio, if the game window loses focus.
 */
TbBool mute_audio_on_focus_lost(void)
{
  return ((features_enabled & Ft_MuteAudioOnLoseFocus) != 0);
}

TbBool is_feature_on(unsigned long feature)
{
  return ((features_enabled & feature) != 0);
}

void set_skip_heart_zoom_feature(TbBool enable)
{
  if (enable)
    features_enabled |= Ft_SkipHeartZoom;
  else
    features_enabled &= ~Ft_SkipHeartZoom;
}

TbBool get_skip_heart_zoom_feature(void)
{
  return ((features_enabled & Ft_SkipHeartZoom) != 0);
}

/**
 * Returns copy of the requested language string in lower case.
 */
const char *get_language_lwrstr(int lang_id)
{
    const char* src = get_conf_parameter_text(lang_type, lang_id);
#if (BFDEBUG_LEVEL > 0)
  if (strlen(src) != 3)
      WARNLOG("Bad text code for language index %d",(int)lang_id);
#endif
  static char lang_str[4];
  snprintf(lang_str, 4, "%s", src);
  make_lowercase(lang_str);
  return lang_str;
}

TbBool prepare_diskpath(char *buf,long buflen)
{
    int i = strlen(buf) - 1;
    if (i >= buflen)
        i = buflen - 1;
    if (i < 0)
        return false;
    // Strip trailing path separators and whitespace.
    while (i > 0)
    {
        if ((buf[i] != '\\') && (buf[i] != '/') &&
            ((unsigned char)(buf[i]) > 32))
            break;
        i--;
    }
    // Also strip trailing "/." and "\." (current-directory components).
    // This normalises paths like "ux0:data/keeperfx/./" -> "ux0:data/keeperfx"
    // which result from resolving a relative INSTALL_PATH such as "./".
    while (i >= 1 && buf[i] == '.' && (buf[i-1] == '/' || buf[i-1] == '\\'))
    {
        i -= 2; // drop the "/."
        // strip any additional trailing separators left behind
        while (i > 0 && (buf[i] == '/' || buf[i] == '\\'))
            i--;
    }
    buf[i+1]='\0';
    return true;
}

static void load_file_configuration(const char *fname, const char *sname, const char *config_textname, unsigned short flags)
{
  TbBool result = kfx_cfg_load(fname);
  if (!result && (flags & CnfLd_IgnoreErrors) == 0) {
    WARNMSG("Failed to load %s file \"%s\".", config_textname, sname);
  }
}

static void load_configuration_for_mod(const struct ModConfigItem *mod_item)
{
    char mod_dir[256] = {0}, config_textname[256] = {0};
    sprintf(mod_dir, "%s/%s", MODS_DIR_NAME, mod_item->name);
    sprintf(config_textname, "Mod config '%s'", mod_item->name);

    char *fname = get_mod_file_path_fmt(mod_dir, FGrp_Main, "%s", keeper_config_file);
    load_file_configuration(fname, keeper_config_file, config_textname, CnfLd_IgnoreErrors);
}

static void load_configuration_for_mod_list(const struct ModConfigItem *mod_items, long mod_cnt)
{
    for (long i=0; i<mod_cnt; i++)
    {
        const struct ModConfigItem *mod_item = mod_items + i;
        if (mod_item->state.mod_dir == 0)
            continue;

        load_configuration_for_mod(mod_item);
    }
}

void load_configuration_for_mod_all(void)
{
    if (mods_conf.after_base_cnt > 0)
    {
        load_configuration_for_mod_list(mods_conf.after_base_item, mods_conf.after_base_cnt);
    }

    if (mods_conf.after_campaign_cnt > 0)
    {
        load_configuration_for_mod_list(mods_conf.after_campaign_item, mods_conf.after_campaign_cnt);
    }

    if (mods_conf.after_map_cnt > 0)
    {
        load_configuration_for_mod_list(mods_conf.after_map_item, mods_conf.after_map_cnt);
    }
}

short load_configuration(void)
{
  // Variables to use when recognizing parameters
  SYNCDBG(4,"Starting");
  // Preparing config file name and checking the file
  strcpy(install_info.inst_path,"");
  // Set default runtime directory and load the config file
  strncpy(keeper_runtime_directory, PlatformManager_GetDataPath(), sizeof(keeper_runtime_directory)-1);
  keeper_runtime_directory[sizeof(keeper_runtime_directory)-1] = '\0';
  // Config file variables
  const char* sname; // Filename
  const char* fname; // Filepath

  if (start_params.ignore_mods == false)
  {
      load_mods_order_config_file();
      recheck_all_mod_exist();
  }
  else
  {
      SYNCMSG("Mod loading skipped");
  }

  // Check if custom config file is set '-config <file>'
  if (start_params.overrides[Clo_ConfigFile])
  {
    // Check if config override contains either '\\' or '/'
    // This means we'll use the absolute path to the config file
    if (strchr(start_params.config_file, '\\') != NULL || strchr(start_params.config_file, '/') != NULL) {
        // Get filename
        const char *backslash = strrchr(start_params.config_file, '\\');
        const char *slash = strrchr(start_params.config_file, '/');
        const char *last_separator = backslash > slash ? backslash : slash;
        sname = last_separator ? last_separator + 1 : start_params.config_file;
        // Get filepath
        fname = start_params.config_file; // Absolute path
    } else {
        sname = start_params.config_file;
        fname = prepare_file_path(FGrp_Main, sname);
    }
  }
  else
  {
    sname = keeper_config_file;
    fname = prepare_file_path(FGrp_Main, sname);
  }

  const char *config_textname = "Base config";
  load_file_configuration(fname, sname, config_textname, 0);

  load_configuration_for_mod_all();

  // Returning if the setting are valid
  return (install_info.lang_id > 0) && (install_info.inst_path[0] != '\0');
}

/** CmdLine overrides allow settings from the command line to override the default settings, or those set in the config file.
 *
 * See enum CmdLineOverrides and struct StartupParameters -> TbBool overrides[CMDLINE_OVERRIDES].
 */
void process_cmdline_overrides(void)
{
  // Use CD for music rather than OGG files
  if (start_params.overrides[Clo_CDMusic])
  {
    features_enabled &= ~Ft_NoCdMusic;
  }
}

TbBool write_keeperfx_cfg(void)
{
    const char* fname = prepare_file_path(FGrp_Main, keeper_config_file);
    return kfx_cfg_save(fname);
}

TbBool write_keeperfx_cfg_all(void)
{
    const char* fname = prepare_file_path(FGrp_Main, keeper_config_file);
    return kfx_cfg_save_all(fname);
}

int parse_draw_fps_config_val(const char *arg, int32_t *fps_draw_main, int32_t *fps_draw_secondary)
{
  int cnt = 0, val1 = 0, val2 = 0;
  long len = strlen(arg);
  int32_t pos = 0;
  char word_buf[32];
  for (int i=0; i<2; i++)
  {
    if (get_conf_parameter_single(arg,&pos,len,word_buf,sizeof(word_buf)) <= 0)
      break;

    switch (i)
    {
    case 0:
      if (strcasecmp(word_buf, "auto") == 0) {
        val1=-1;
        cnt++;
      } else {
        val1 = atoi(word_buf);
        if (val1 >= 0){
          cnt++;
        } else {
          i=2; // jump out for loop
          break;
        }
      }
      break;
    case 1:
        val2 = atoi(word_buf);
        if (val2 >= 0){
          cnt++;
        } else {
          i=2; // jump out for loop
          break;
        }
      break;
    }
  }

  if (cnt > 0) {
    *fps_draw_main = val1;
  }
  if (cnt > 1) {
    *fps_draw_secondary = val2;
  }

  return cnt;
}


/******************************************************************************/
