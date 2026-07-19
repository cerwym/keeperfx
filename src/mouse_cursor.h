/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file mouse_cursor.h
 *     Header file for mouse_cursor.c.
 * @par Purpose:
 *     Selection of the mouse pointer icon shown for the current game/UI state.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_MOUSE_CURSOR_H
#define DK_MOUSE_CURSOR_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

enum MousePointerGraphics {
    MousePG_Invisible = 0,
    MousePG_Arrow,
    MousePG_Pickaxe,
    MousePG_Sell,
    MousePG_Query,
    MousePG_PlaceTrap01,
    MousePG_PlaceTrap02,
    MousePG_PlaceTrap03,
    MousePG_PlaceTrap04,
    MousePG_PlaceTrap05,
    MousePG_PlaceTrap06,
    MousePG_PlaceDoor01,
    MousePG_PlaceDoor02,
    MousePG_PlaceDoor03,
    MousePG_PlaceDoor04,
    MousePG_DenyMark,
    MousePG_SpellCharge0,
    MousePG_SpellCharge1,
    MousePG_SpellCharge2,
    MousePG_SpellCharge3,
    MousePG_SpellCharge4,
    MousePG_SpellCharge5,
    MousePG_SpellCharge6,
    MousePG_SpellCharge7,
    MousePG_SpellCharge8,
    MousePG_PlaceRoom01,
    MousePG_PlaceRoom02,
    MousePG_PlaceRoom03,
    MousePG_PlaceRoom04,
    MousePG_PlaceRoom05,
    MousePG_PlaceRoom06,
    MousePG_PlaceRoom07,
    MousePG_PlaceRoom08,
    MousePG_PlaceRoom09,
    MousePG_PlaceRoom10,
    MousePG_PlaceRoom11,
    MousePG_PlaceRoom12,
    MousePG_PlaceRoom13,
    MousePG_PlaceRoom14,
    MousePG_LockMark,
    // 40..144 are spell pointers
    MousePG_Unkn40,
    MousePG_Unkn41,
    MousePG_Unkn42,
    MousePG_Unkn43,
    MousePG_Unkn44,
    MousePG_Unkn45,
    MousePG_Unkn46,
    MousePG_Unkn47,
    MousePG_Unkn48,
    MousePG_Unkn49,
    MousePG_Unkn96      = 96,
    MousePG_Unkn97      = 97,
    MousePG_Unkn98      = 98,
    MousePG_Unkn99      = 99,
    MousePG_Unkn100     = 100,
    MousePG_Unkn101     = 101,
    MousePG_Unkn102     = 102,
    MousePG_Unkn103     = 103,
    MousePG_PlaceImpRock = 144,
    MousePG_PlaceGold    = 145,
    MousePG_PlaceEarth   = 146,
    MousePG_PlaceWall    = 147,
    MousePG_PlacePath    = 148,
    MousePG_PlaceClaimed = 149,
    MousePG_PlaceLava    = 150,
    MousePG_PlaceWater   = 151,
    MousePG_PlaceGems    = 152,
    MousePG_MkDigger     = 153,
    MousePG_MkCreature   = 154,
    MousePG_MvCreature   = 155,
    MousePG_Mystery      = 156,
    MousePG_PlaceTrap07  = 157,
    MousePG_PlaceTrap08  = 158,
    MousePG_PlaceTrap09  = 159,
    MousePG_PlaceTrap10  = 160,
    MousePG_PlaceTrap11  = 161,
    MousePG_PlaceTrap12  = 162,
    MousePG_PlaceTrap13  = 163,
    MousePG_PlaceTrap14  = 164,
    MousePG_PlaceRoom15  = 165,
    // 166..181 are place trap pointers with spell icons
    MousePG_Unkn166     = 166,
    MousePG_Unkn167     = 167,
    MousePG_Unkn168     = 168,
    MousePG_Unkn169     = 169,
    MousePG_Unkn170     = 170,
    MousePG_Unkn171     = 171,
    MousePG_Unkn172     = 172,
    MousePG_Unkn173     = 173,
    MousePG_Unkn174     = 174,
    MousePG_Unkn175     = 175,
    MousePG_Unkn176     = 176,
    MousePG_Unkn177     = 177,
    MousePG_Unkn178     = 178,
    MousePG_Unkn179     = 179,
    MousePG_Unkn180     = 180,
    MousePG_Unkn181     = 181,
    MousePG_Pickaxe2     = 473,
};
/******************************************************************************/
extern struct TbSpriteSheet *pointer_sprites;

void load_pointer_file(short hi_res);
void unload_pointer_file(short hi_res);

TbBool set_pointer_graphic_none(void);
TbBool set_pointer_graphic_menu(void);
TbBool set_pointer_graphic_spell(long spridx, long frame);
TbBool set_pointer_graphic(long ptr_idx);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
