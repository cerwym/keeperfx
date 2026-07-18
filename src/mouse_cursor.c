/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file mouse_cursor.c
 *     Selection of the mouse pointer icon shown for the current game/UI state.
 * @par Purpose:
 *     Loads the pointer sprite sheet and picks which sprite/hotspot to show
 *     for a given MousePointerGraphics index or spell charge.
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
#include "mouse_cursor.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_mouse.h"
#include "bflib_sprite.h"

#include "config_spritecolors.h"
#include "config_keeperfx.h"
#include "custom_sprites.h"
#include "gui_draw.h"
#include "player_data.h"
#include "sprites.h"
#include "kfx/assets/SpriteSheetManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct TbSpriteSheet * pointer_sprites = NULL;
/******************************************************************************/

void load_pointer_file(short hi_res)
{
    SpriteSheetMgr_Load(&pointer_sprites, "data/pointer64.dat", "data/pointer64.tab");
    if (!pointer_sprites) ERRORLOG("Unable to load pointer sprites");
}

void unload_pointer_file(short hi_res)
{
    set_pointer_graphic_none();
    SpriteSheetMgr_Free(&pointer_sprites);
}

TbBool set_pointer_graphic_none(void)
{
  LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
  return true;
}

TbBool set_pointer_graphic_menu(void)
{
  if (frontend_sprite == NULL)
  {
    WARNLOG("Frontend sprites not loaded, setting pointer to none");
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
    return false;
  }
  LbMouseChangeSpriteAndHotspot(get_frontend_sprite(GFS_cursor_horny), 0, 0);
  return true;
}

TbBool set_pointer_graphic_spell(long spridx, long frame)
{
    long i;
    long x;
    long y;
    SYNCDBG(8, "Setting to sprite %d", (int)spridx);
    if (pointer_sprites == NULL)
    {
        WARNLOG("Pointer sprites not loaded, setting to none");
        LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
        return false;
  }
  if (is_feature_on(Ft_BigPointer))
  {
    y = 32;
    x = 32;
    // frame is the game turn; the 64px spell pointers are stored as runs of
    // 8 consecutive animation frames, so this advances one frame per turn.
    i = spridx + (frame % 8);
  } else
  {
    y = 78;
    x = 26;
    i = spridx;
  }
  const struct TbSprite* spr = NULL;

  if (is_custom_icon(i))
  {
      spr = get_new_icon_sprite(i);
      SYNCDBG(8,"Activating pointer %ld", i);
      LbMouseChangeSpriteAndHotspot(spr, x/2, y/2);
  }
  else
  {
      if (i >= 0 && i < num_sprites(pointer_sprites))
      {
          spr = get_sprite(pointer_sprites, i);
          SYNCDBG(8,"Activating pointer %ld", i);
          LbMouseChangeSpriteAndHotspot(spr, x/2, y/2);
      } else
      {
          WARNLOG("Sprite %d exceeds buffer, setting pointer to none",(int)i);
          LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
      }
  }
  return true;
}

TbBool set_pointer_graphic(long ptr_idx)
{
    long x;
    long y;
    const struct TbSprite* spr;
    SYNCDBG(8, "Setting to %d", (int)ptr_idx);
    if (pointer_sprites == NULL)
    {
        WARNLOG("Pointer sprites not loaded, setting to none");
        LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
        return false;
    }


  switch (ptr_idx)
  {
  case MousePG_Invisible:
  case MousePG_Arrow:
  case MousePG_Pickaxe:
  case MousePG_Query:
  case MousePG_DenyMark:
  case MousePG_Pickaxe2:
    ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case MousePG_Sell:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 17; y = 29;
      break;
  case MousePG_PlaceTrap01:
  case MousePG_PlaceTrap02:
  case MousePG_PlaceTrap03:
  case MousePG_PlaceTrap04:
  case MousePG_PlaceTrap05:
  case MousePG_PlaceTrap06:
  case MousePG_PlaceTrap07:
  case MousePG_PlaceTrap08:
  case MousePG_PlaceTrap09:
  case MousePG_PlaceTrap10:
  case MousePG_PlaceTrap11:
  case MousePG_PlaceTrap12:
  case MousePG_PlaceTrap13:
  case MousePG_PlaceTrap14:
  case MousePG_PlaceDoor01:
  case MousePG_PlaceDoor02:
  case MousePG_PlaceDoor03:
  case MousePG_PlaceDoor04:
  case MousePG_Mystery:
      // 166..181 are place trap pointers with spell icons
  case MousePG_Unkn166:
  case MousePG_Unkn167:
  case MousePG_Unkn168:
  case MousePG_Unkn169:
  case MousePG_Unkn170:
  case MousePG_Unkn171:
  case MousePG_Unkn172:
  case MousePG_Unkn173:
  case MousePG_Unkn174:
  case MousePG_Unkn175:
  case MousePG_Unkn176:
  case MousePG_Unkn177:
  case MousePG_Unkn178:
  case MousePG_Unkn179:
  case MousePG_Unkn180:
  case MousePG_Unkn181:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  case  MousePG_SpellCharge0:
  case  MousePG_SpellCharge1:
  case  MousePG_SpellCharge2:
  case  MousePG_SpellCharge3:
  case  MousePG_SpellCharge4:
  case  MousePG_SpellCharge5:
  case  MousePG_SpellCharge6:
  case  MousePG_SpellCharge7:
  case  MousePG_SpellCharge8:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 20; y = 20;
      break;
  case  MousePG_PlaceRoom01:
  case  MousePG_PlaceRoom02:
  case  MousePG_PlaceRoom03:
  case  MousePG_PlaceRoom04:
  case  MousePG_PlaceRoom05:
  case  MousePG_PlaceRoom06:
  case  MousePG_PlaceRoom07:
  case  MousePG_PlaceRoom08:
  case  MousePG_PlaceRoom09:
  case  MousePG_PlaceRoom10:
  case  MousePG_PlaceRoom11:
  case  MousePG_PlaceRoom12:
  case  MousePG_PlaceRoom13:
  case  MousePG_PlaceRoom14:
  case  MousePG_PlaceRoom15:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  case  MousePG_LockMark:
  // 40..144 are spell pointers
  case  MousePG_Unkn47:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case  MousePG_Unkn96:
  case  MousePG_Unkn97:
  case  MousePG_Unkn98:
  case  MousePG_Unkn99:
  case  MousePG_Unkn100:
  case  MousePG_Unkn101:
  case  MousePG_Unkn102:
  case  MousePG_Unkn103:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 15;
      break;
  case MousePG_PlaceImpRock:
  case MousePG_PlaceGold:
  case MousePG_PlaceEarth:
  case MousePG_PlaceWall:
  case MousePG_PlacePath:
  case MousePG_PlaceClaimed:
  case MousePG_PlaceLava:
  case MousePG_PlaceWater:
  case MousePG_PlaceGems:
  case MousePG_MkDigger:
  case MousePG_MkCreature:
  case MousePG_MvCreature:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      x = 12; y = 38;
      break;
  default:
      ptr_idx = get_player_colored_pointer_icon_idx(ptr_idx,my_player_number);
      spr = get_new_icon_sprite(ptr_idx);
      if (spr != NULL)
      {
          LbMouseChangeSpriteAndHotspot(spr, spr->SWidth/2, spr->SHeight);
          return true;
      }
    WARNLOG("Unrecognized Mouse Pointer index, %ld",ptr_idx);
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
    return false;
  }
  if (ptr_idx >= 0 && ptr_idx < num_sprites(pointer_sprites)) {
    spr = get_sprite(pointer_sprites, ptr_idx);
    LbMouseChangeSpriteAndHotspot(spr, x, y);
  } else {
    WARNLOG("Sprite %d exceeds buffer, setting pointer to none",(int)ptr_idx);
    LbMouseChangeSpriteAndHotspot(NULL, 0, 0);
  }
  return true;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
