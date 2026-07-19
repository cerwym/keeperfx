/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidmode.c
 *     Video mode switching/setting function.
 * @par Purpose:
 *     Functions to change video mode in DK way.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     05 Jan 2009 - 12 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "vidmode.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fmvids.h"
#include "bflib_video.h"
#include "bflib_mouse.h"
#include "bflib_sprite.h"
#include "bflib_dernc.h"
#include "bflib_sprfnt.h"
#include "bflib_filelst.h"


#include "vidfade.h"
#include "mouse_cursor.h"
#include "front_simple.h"
#include "front_landview.h"
#include "frontend.h"
#include "kfx/sprite_resources.h"
#include "gui_draw.h"
#include "gui_parchment.h"
#include "gui_topmsg.h"
#include "engine_redraw.h"
#include "engine_textures.h"
#include "config_keeperfx.h"
#include "lens_api.h"
#include "config_settings.h"
#include "game_legacy.h"
#include "creature_graphics.h"
#include "keeperfx.hpp"
#include "renderer/RendererManager.h"
#include "kfx/assets/SpriteSheetManager.h"
#include "kfx/assets/FontManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
TbScreenMode switching_vidmodes[] = {
  Lb_SCREEN_MODE_320_200_8,
  Lb_SCREEN_MODE_640_480_8,
  Lb_SCREEN_MODE_INVALID,
  Lb_SCREEN_MODE_INVALID,
  Lb_SCREEN_MODE_INVALID,
  Lb_SCREEN_MODE_INVALID,
  };


static TbScreenMode failsafe_vidmode = Lb_SCREEN_MODE_320_200_8;
static TbScreenMode movies_vidmode   = Lb_SCREEN_MODE_640_480_8;
static TbScreenMode frontend_vidmode = Lb_SCREEN_MODE_640_480_8;

//struct IPOINT_2D units_per_pixel;
unsigned short units_per_pixel_min;
unsigned short units_per_pixel_width;
unsigned short units_per_pixel_height;
unsigned short units_per_pixel_menu_height;
unsigned short units_per_pixel_best;
unsigned short units_per_pixel_menu;
unsigned short units_per_pixel_landview;
unsigned short units_per_pixel_landview_frame;
unsigned short units_per_pixel_ui;
unsigned long aspect_ratio_factor_HOR_PLUS;
unsigned long aspect_ratio_factor_HOR_PLUS_AND_VERT_PLUS;
unsigned long first_person_horizontal_fov;
unsigned long first_person_vertical_fov;
unsigned long landview_frame_movement_scale_x;
unsigned long landview_frame_movement_scale_y;
long base_mouse_sensitivity = 256;

static TbBool force_video_mode_reset = true;

TbBool MinimalResolutionSetup;
/******************************************************************************/


extern struct TbLoadFiles gui_load_files_320[];
extern struct TbLoadFiles gui_load_files_640[];
extern struct TbLoadFiles front_load_files_minimal_640[];

static TbBool switch_to_next_video_mode(void);
/******************************************************************************/

/**
 * Loads VGA 256 graphics files, for high resolution modes.
 * @return Returns true if all files were loaded, false otherwise.
 */
static short LoadVRes256Data(long scrbuf_size)
{
    // Update size of the parchment buffer, as it is also used as screen buffer
    if (scrbuf_size < 640*480)
        scrbuf_size = 640*480;
    gui_load_files_640[1].SLength = scrbuf_size;
    // Load the files
    FontMgr_Load(&winfont, "data/font2-64.dat", "data/font2-64.tab");
    FontMgr_Load(&font_sprites, "data/font1-64.dat", "data/font1-64.tab");
    SpriteSheetMgr_Load(&button_sprites, "data/gui1-64.dat", "data/gui1-64.tab");
    SpriteSheetMgr_Load(&gui_panel_sprites, "data/gui2-64.dat", "data/gui2-64.tab");
    if (!winfont || !font_sprites || !button_sprites || !gui_panel_sprites || LbDataLoadAll(gui_load_files_640)) {
        return 0;
    }
    RendererNotifySpritesReloaded();
    return 1;
}

static void FreeVRes256Data(void)
{
    FontMgr_Free(&winfont);
    FontMgr_Free(&font_sprites);
    SpriteSheetMgr_Free(&button_sprites);
    SpriteSheetMgr_Free(&gui_panel_sprites);
    LbDataFreeAll(gui_load_files_640);
}

static short LoadVResMinimal(void)
{
    SpriteSheetMgr_Load(&button_sprites, "data/gui1-32.dat", "data/gui1-32.tab");
    FontMgr_Load(&frontend_font[0], "ldata/frontft1.dat", "ldata/frontft1.tab");
    FontMgr_Load(&frontend_font[1], "ldata/frontft2.dat", "ldata/frontft2.tab");
    FontMgr_Load(&frontend_font[2], "ldata/frontft3.dat", "ldata/frontft3.tab");
    FontMgr_Load(&frontend_font[3], "ldata/frontft4.dat", "ldata/frontft4.tab");
    if (!button_sprites || !frontend_font[0] || !frontend_font[1] || !frontend_font[2] ||
        !frontend_font[3] || LbDataLoadAll(front_load_files_minimal_640) != 0) {
        return 0;
    }
    RendererNotifySpritesReloaded();
    return 1;
}

static void FreeVResMinimal(void)
{
    for (int i = 0; i < FRONTEND_FONTS_COUNT; ++i) {
        FontMgr_Free(&frontend_font[i]);
    }
    SpriteSheetMgr_Free(&button_sprites);
    LbDataFreeAll(front_load_files_minimal_640);
}

/**
 * Loads MCGA graphics files, for low resolution mode.
 * It is modified version of LbDataLoadAll, optimized for maximum
 * game speed on very slow machines.
 * @return Returns true if all files were loaded, false otherwise.
 */
static short LoadMcgaData(void)
{
    int ferror = 0;
    int i = 0;
    struct TbLoadFiles* t_lfile = &gui_load_files_320[i];
    while (t_lfile->Start != NULL)
    {
        // Don't allow loading flags
        t_lfile->Flags = 0;
        int ret_val = LbDataLoad(t_lfile, NULL, NULL);
        if (ret_val == -100)
        {
            ERRORLOG("Can't allocate memory for MCGA files element \"%s\".", t_lfile->FName);
            ferror++;
        }
        else if (ret_val == -101)
        {
            ERRORLOG("Can't load MCGA file \"%s\".", t_lfile->FName);
            ferror++;
        }
        i++;
        t_lfile = &gui_load_files_320[i];
  }
  SpriteSheetMgr_Load(&button_sprites, "data/gui1-32.dat", "data/gui1-32.tab");
  FontMgr_Load(&winfont, "data/font2-32.dat", "data/font2-32.tab");
  FontMgr_Load(&font_sprites, "data/font1-32.dat", "data/font1-32.tab");
  SpriteSheetMgr_Load(&gui_panel_sprites, "data/gui2-32.dat", "data/gui2-32.tab");
  if (button_sprites && winfont && font_sprites && gui_panel_sprites && (ferror == 0)) {
      RendererNotifySpritesReloaded();
      return 1;
  }
  return 0;
}

static void FreeMcgaData(void)
{
    LbDataFreeAll(gui_load_files_320);
    FontMgr_Free(&winfont);
    FontMgr_Free(&font_sprites);
    SpriteSheetMgr_Free(&button_sprites);
    SpriteSheetMgr_Free(&gui_panel_sprites);
}

void set_game_vidmode(uint i, TbScreenMode nmode)
{
  switching_vidmodes[i%MAX_GAME_VIDMODE_COUNT]=nmode;
}

static TbScreenMode get_game_vidmode(uint i)
{
  return switching_vidmodes[i%MAX_GAME_VIDMODE_COUNT];
}

static TbScreenMode try_failsafe_vidmode(void)
{
  // Check the failsafe mode
  if (!LbScreenIsModeAvailable(failsafe_vidmode, display_id))
  {
      ERRORLOG("Failsafe video mode (mode %d) is invalid.",(int)failsafe_vidmode);
      return Lb_SCREEN_MODE_INVALID;
  }
  return failsafe_vidmode;
}

static TbScreenMode get_failsafe_vidmode(void)
{
  return failsafe_vidmode;
}

void set_failsafe_vidmode(TbScreenMode nmode)
{
  failsafe_vidmode=nmode;
}

TbScreenMode get_movies_vidmode(void)
{
  return movies_vidmode;
}

void set_movies_vidmode(TbScreenMode nmode)
{
  movies_vidmode=nmode;
}

TbScreenMode get_frontend_vidmode(void)
{
  return frontend_vidmode;
}

void set_frontend_vidmode(TbScreenMode nmode)
{
  frontend_vidmode=nmode;
}

char *get_vidmode_name(TbScreenMode mode)
{
    TbScreenModeInfo* curr_mdinfo = LbScreenGetModeInfo(mode);
    return curr_mdinfo->Desc;
}

static TbScreenModeInfo* begin_screen_mode_setup(TbScreenMode nmode, TbScreenMode *old_mode_out)
{
  TbScreenModeInfo* new_mdinfo = LbScreenGetModeInfo(nmode);
  TbScreenMode old_mode = RendererActiveMode();
  TbScreenModeInfo* old_mdinfo = LbScreenGetModeInfo(old_mode);
  if (!(old_mdinfo->VideoFlags & Lb_VF_FILLALL))
  {
    display_id = LbGetCurrentDisplayIndex(); // get current display
  }
  *old_mode_out = old_mode;
  return new_mdinfo;
}

/**
 * Set up a new screen mode suitable for playing the game.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @param falisafe If TRUE the the failsafe resolution will be used if nmode is not available
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
static TbScreenMode setup_screen_mode(TbScreenMode nmode, TbBool failsafe)
{
  SYNCDBG(4,"Setting up mode %d",(int)nmode);
  TbScreenMode old_mode;
  TbScreenModeInfo* new_mdinfo = begin_screen_mode_setup(nmode, &old_mode);
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
    if (failsafe)
    {
      ERRORLOG("Unable to setup screen resolution %s (mode %d), trying failsafe mode", new_mdinfo->Desc,(int)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    else
    {
      ERRORLOG("Unable to setup screen resolution %s (mode %d)", new_mdinfo->Desc,(int)nmode);
      return Lb_SCREEN_MODE_INVALID;
    }
    new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  if (!force_video_mode_reset)
  {
    if ((nmode == old_mode) && (!MinimalResolutionSetup))
    {
      SYNCDBG(6,"Mode %d already active, no changes.",(int)nmode);
      return nmode;
    }
  }
  TbBool hi_res = ((RendererScreenHeight() < 400) ? false : true);
  long lens_mem = game.applied_lens_type;
  TbBool was_minimal_res = (MinimalResolutionSetup || force_video_mode_reset);
  set_pointer_graphic_none();
  if (RendererScreenHeight() < 200)
  {
      WARNLOG("Unhandled previous Screen Mode %d, Reset skipped",(int)old_mode);
  } else
  {
    if (!MinimalResolutionSetup)
    {
      reset_eye_lenses();
      reset_heap_manager();
      unload_pointer_file(hi_res);
    }
    if (nmode != old_mode)
        RendererResetScreen(false);
    if (MinimalResolutionSetup) {
      if (hi_res) {
        FreeVResMinimal();
      }
    } else {
      if (hi_res) {
        FreeVRes256Data();
      } else {
        FreeMcgaData();
      }
    }
    if (!hi_res) ERRORLOG("MCGA Minimal not allowed (Reset)");
    MinimalResolutionSetup = false;
  }
  hi_res = ((new_mdinfo->Height < 400) ? false : true);
  if (new_mdinfo->Height < 200)
  {
      ERRORLOG("Unhandled Screen Mode %d, setup failed",(int)nmode);
      force_video_mode_reset = true;
      return Lb_SCREEN_MODE_INVALID;
  } else
  {
    SYNCDBG(6,"Entering %s mode %d, resolution %dx%d.",hi_res?"hi-res":"low-res",(int)nmode,(int)new_mdinfo->Width,(int)new_mdinfo->Height);
    if (hi_res)
    {
      if (!LoadVRes256Data((long)new_mdinfo->Width*(long)new_mdinfo->Height))
      {
        ERRORLOG("Unable to load VRes256 data files");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    else
    {
      if (!LoadMcgaData())
      {
        ERRORLOG("Loading Mcga files failed");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
    }
    if ((nmode != old_mode) || (was_minimal_res))
    {
        if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, (hi_res ? 1 : 2), 0) < Lb_SUCCESS)
        {
          ERRORLOG("Unable to setup screen resolution %s (mode %d)", new_mdinfo->Desc,(int)nmode);
          force_video_mode_reset = true;
          return Lb_SCREEN_MODE_INVALID;
        }
    }
    load_pointer_file(hi_res);
  }
  RendererClearScreen(0);
  RendererPresentFrame();
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  if (parchment_loaded)
    reload_parchment_file(hi_res);
  reinitialise_eye_lens(lens_mem);
  setup_heap_manager();
  force_video_mode_reset = false;
  SYNCDBG(8,"Finished");
  return nmode;
}

TbBool update_screen_mode_data(long width, long height)
{
  // if ((width >= 640) && (height >= 400))
  // {
    pixel_size = 1;
    /*
  } else
  {
    pixel_size = 2;
  }
  */
  long psize = pixel_size;

  MyScreenWidth = width * psize;
  MyScreenHeight = height * psize;
  pixels_per_block = 16 * psize;


  // Adjust scaling factor (units per pixel) based on window resolution compared to the original DK resolutions
  // low-res - units per pixel = 8, low-res - units per pixel = 16 (or upp min is low-res = 4, high-res = 10)

  // In-game scaling (DK original: low-res - 320x200, high-res - 640x400)
  units_per_pixel = (width>height?width:height)/40;// originally was 16 for hires, 8 for lores
  units_per_pixel_min = (width>height?height:width)/40;// originally 10 for hires
  units_per_pixel_width = width/40; // 8 for low res, 16 is "kfx default"
  units_per_pixel_height = height/25; // 8 for low res, 16 is "kfx default"
  units_per_pixel_best = ((is_ar_wider_than_original(width, height)) ? units_per_pixel_height : units_per_pixel_width); // If the screen is wider than 16:10 the height is used; if the screen is narrower than 16:10 the width is used.

  // In-game scaling: UI (for the side bar menu and escape menu)
  long ui_scale = UI_NORMAL_SIZE; // UI_NORMAL_SIZE, UI_HALF_SIZE, or UI_DOUBLE_SIZE (not fully implemented yet)
  units_per_pixel_ui = resize_ui(units_per_pixel_best, ui_scale);

  // In-game scaling: Posession Mode (a 3D 1st person perspective camera)
  calculate_aspect_ratio_factor(width, height);
  first_person_vertical_fov = DEFAULT_FIRST_PERSON_VERTICAL_FOV;
  first_person_horizontal_fov = FOV_based_on_aspect_ratio();

  // Main menu scaling (DK original: 640x480)
  units_per_pixel_menu_height = height/30; // 16 is "kfx default" (640x480)
  units_per_pixel_menu = ((is_menu_ar_wider_than_original(width, height)) ? units_per_pixel_menu_height : units_per_pixel_width); // If the screen is wider than 4:3 the height is used; if the screen is narrower than 4:3 the width is used.

  // Main menu scaling: Campaign map "land view" screen (including the window frame)
  calculate_landview_upp(width, height, LANDVIEW_MAP_WIDTH, LANDVIEW_MAP_HEIGHT); // 16 is "kfx default" for 640x480 game window (1x), a 960x720 frame (1.5x), and a 1280x960 landview (2x)

  LbMouseChangeMoveRatio(base_mouse_sensitivity, base_mouse_sensitivity);
  LbMouseSetPointerHotspot(0, 0);
  RendererSetViewport(0, 0, RendererScreenWidth(), RendererScreenHeight());
  LbTextSetWindow(0, 0, RendererScreenWidth(), RendererScreenHeight());
  LbMouseSetup(NULL);
  return true;
}

/**
 * Set up a new screen mode suitable for the frontend or movie playback.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode setup_screen_mode_minimal(TbScreenMode nmode)
{
  SYNCDBG(4,"Setting up mode %d",(int)nmode);
  TbScreenMode old_mode;
  TbScreenModeInfo* new_mdinfo = begin_screen_mode_setup(nmode, &old_mode);
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %d), trying failsafe mode", new_mdinfo->Desc,(int)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
      new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  if (!force_video_mode_reset)
  {
    if ((nmode == old_mode) && (MinimalResolutionSetup))
    {
      // Ensure minimal VRes resources are loaded even if the intro was skipped.
      // initial_setup() sets MinimalResolutionSetup=true without calling
      // LoadVResMinimal(), so button_sprites and frontend_font[] may be NULL.
      if (!button_sprites)
      {
        TbBool hi_res = (new_mdinfo->Height >= 400);
        if (hi_res)
        {
          frontend_load_data_from_cd();
          if (!LoadVResMinimal())
          {
            ERRORLOG("Unable to load minimal VRes front files");
            force_video_mode_reset = true;
            return Lb_SCREEN_MODE_INVALID;
          }
          frontend_load_data_reset();
        }
      }
      SYNCDBG(6,"Mode %d already active, no changes.",(int)nmode);
      return nmode;
    }
  }
  TbBool hi_res = ((RendererScreenHeight() < 400) ? false : true);
  if (RendererScreenHeight() < 200)
  {
    WARNLOG("Unhandled previous Screen Mode %d, Reset skipped",(int)old_mode);
  } else
  {
    if (!MinimalResolutionSetup)
    {
      reset_eye_lenses();
      reset_heap_manager();
    }
    if ((!MinimalResolutionSetup && !hi_res) || (MinimalResolutionSetup && hi_res))
      unload_pointer_file(hi_res);
    if ((nmode != old_mode) || (force_video_mode_reset))
      RendererResetScreen(false);
    if (hi_res)
    {
      if (MinimalResolutionSetup) {
        FreeVResMinimal();
      } else {
        FreeVRes256Data();
      }
    }
    else
    {
      if (!MinimalResolutionSetup) FreeMcgaData();
    }
    MinimalResolutionSetup = false;
  }
  hi_res = ((new_mdinfo->Height < 400) ? false : true);
  if (new_mdinfo->Height < 200)
  {
      ERRORLOG("Unhandled Screen Mode %d, setup failed",(int)nmode);
      force_video_mode_reset = true;
      return Lb_SCREEN_MODE_INVALID;
  } else
  {
    SYNCDBG(17,"Preparing minimal %s resolution mode",(hi_res ? "high" : "low"));
    MinimalResolutionSetup = true;
    if (hi_res)
    {
      frontend_load_data_from_cd();
      if (!LoadVResMinimal())
      {
        ERRORLOG("Unable to load VRes256 front_load minimal files");
        force_video_mode_reset = true;
        return Lb_SCREEN_MODE_INVALID;
      }
      frontend_load_data_reset();
    }

    if ((nmode != old_mode) || (force_video_mode_reset))
    {
        if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, (hi_res ? 1 : 2), 0) < Lb_SUCCESS)
        {
          ERRORLOG("Unable to setup screen resolution %s (mode %d)", new_mdinfo->Desc,(int)nmode);
          force_video_mode_reset = true;
          return Lb_SCREEN_MODE_INVALID;
        }
    }
  }
  RendererClearScreen(0);
  RendererPresentFrame();
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  force_video_mode_reset = false;
  return nmode;
}

/**
 * Set up a new screen mode with a blank black screen.
 *
 * @param nmode The mode (index number) that we want to change to.
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode setup_screen_mode_zero(TbScreenMode nmode)
{
  SYNCDBG(4,"Setting up mode %d",(int)nmode);
  TbScreenMode old_mode;
  TbScreenModeInfo* new_mdinfo = begin_screen_mode_setup(nmode, &old_mode);
  // Skip full setup when already in the requested mode to avoid redundant
  // surface recreation and refresh-rate redetection on a visible window.
  if (old_mode == nmode)
  {
    force_video_mode_reset = true;
    return nmode;
  }
  // Check that the desired mode is available for the current display
  if (!LbScreenIsModeAvailable(nmode, display_id))
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %d), trying failsafe mode", new_mdinfo->Desc,(int)nmode);
      nmode = try_failsafe_vidmode();
      if (nmode == Lb_SCREEN_MODE_INVALID)
      {
        return Lb_SCREEN_MODE_INVALID;
      }
      new_mdinfo = LbScreenGetModeInfo(nmode);
  }
  LbPaletteDataFillBlack(engine_palette);
  if (RendererSetupScreen(nmode, new_mdinfo->Width, new_mdinfo->Height, engine_palette, 2, 0) < Lb_SUCCESS)
  {
      ERRORLOG("Unable to setup screen resolution %s (mode %d)", new_mdinfo->Desc,(int)nmode);
      return Lb_SCREEN_MODE_INVALID;
  }
  update_screen_mode_data(new_mdinfo->Width, new_mdinfo->Height);
  force_video_mode_reset = true;
  return nmode;
}

/**
 * Set up a the screen using the mode saved in settings (video_scrnmode).
 *
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
TbScreenMode reenter_video_mode(void)
{
#ifdef PLATFORM_VITA
  // Vita has a fixed 960x544 display; always render the game at 640x480.
  // Ignore the user's INGAME_RES config and saved index — 320x200 is unusable.
  TbScreenMode scrmode = setup_screen_mode(Lb_SCREEN_MODE_640_480_8, false);
#else
  TbScreenMode scrmode = get_game_vidmode(settings.switching_vidmodes_index);
  scrmode = setup_screen_mode(scrmode, false);
#endif
  if (scrmode == Lb_SCREEN_MODE_INVALID)
  {
    // try all of the other switchable video modes before eventually trying the failsafe
    if (!switch_to_next_video_mode())
    {
      return Lb_SCREEN_MODE_INVALID;
    }
  }
  else
  {
    SYNCLOG("set in-game video as %s (mode %d)", get_vidmode_name(scrmode),(int)scrmode);
  }
  return scrmode;
}

/**
 * Switch to the next mode in the list set by the INGAME_RES config setting (these are stored in switching_vidmodes[]).
 *
 * @return Returns the mode that the screen was setup successfully with (or Lb_SCREEN_MODE_INVALID/false when the screen was not setup successfully).
 */
static TbBool switch_to_next_video_mode(void)
{
  uint current = settings.switching_vidmodes_index;
  uint i = current;
  TbBool failsafe = false;
  TbScreenMode scrmode;
  do
  {
    i++;
    if (i>=MAX_GAME_VIDMODE_COUNT)
    {
      i=0;
    }
    if (i == current)
    {
      // we have done a full loop of switching_vidmodes[]
      if (get_game_vidmode(i) == RendererActiveMode())
      {
        SYNCLOG("No new mode to switch to; staying with %s (mode %d).", get_vidmode_name(scrmode),(int)scrmode);
        return true; // only 1 valid video mode, and we are already in it
      }
      // else there are no valid modes in the array, try the failsafe
      scrmode = setup_screen_mode(get_failsafe_vidmode(), false);
      failsafe = true;
      break;
    }
    scrmode = get_game_vidmode(i);
    if (scrmode != Lb_SCREEN_MODE_INVALID)
    {
      // try the next vidmode in switching_vidmodes[]
      scrmode = setup_screen_mode(scrmode, false);
    }
  } while (scrmode == Lb_SCREEN_MODE_INVALID);

  if (scrmode > Lb_SCREEN_MODE_INVALID)
  {
    if (failsafe)
    {
      show_onscreen_msg(turns_per_second * 6, "%s", get_string(856));
    }
    else
    {
      // we managed to switch to a new mode
      show_onscreen_msg(turns_per_second * 6, "%s", get_vidmode_name(scrmode));
      settings.switching_vidmodes_index = i;
      save_settings();
    }
  }
  else
  {
    FatalError = 1;
    exit_keeper = 1;
    return false;
  }
  SYNCLOG("Switched video to %s (mode %d)", get_vidmode_name(scrmode),(int)scrmode);
  return true;
}

/** Needed until its contents are refactored, then we can just call switch_to_next_video_mode from PckA_SwitchScrnRes. */
void switch_to_next_video_mode_wrapper(void)
{
  char percent_x = ((float)lbMouse.MMouseX / (float)(lbMouse.MouseWindowX + lbMouse.MouseWindowWidth)) * 100;
  char percent_y = ((float)lbMouse.MMouseY / (float)(lbMouse.MouseWindowY + lbMouse.MouseWindowHeight)) * 100;

  if (switch_to_next_video_mode() == Lb_SCREEN_MODE_INVALID)
  {
    return;
  }

  TbBool reload_video = (menu_is_active(GMnu_VIDEO));
  if (menu_is_active(GMnu_CREATURE_QUERY1))
  {
    vid_change_query_menu = GMnu_CREATURE_QUERY1;
  }
  else if (menu_is_active(GMnu_CREATURE_QUERY2))
  {
    vid_change_query_menu = GMnu_CREATURE_QUERY2;
  }
  else if (menu_is_active(GMnu_CREATURE_QUERY3))
  {
    vid_change_query_menu = GMnu_CREATURE_QUERY3;
  }
  else if (menu_is_active(GMnu_CREATURE_QUERY4))
  {
    vid_change_query_menu = GMnu_CREATURE_QUERY4;
  }
  reinit_all_menus();
  if (reload_video)
  {
    turn_on_menu(GMnu_VIDEO);
  }
  LbMouseSetPosition(((lbMouse.MouseWindowX + lbMouse.MouseWindowWidth) / 100) * percent_x, ((lbMouse.MouseWindowY + lbMouse.MouseWindowHeight) / 100) * percent_y);
  return;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
