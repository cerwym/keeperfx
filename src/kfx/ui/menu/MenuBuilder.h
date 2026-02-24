/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuBuilder.h
 *     Converts MenuDefinition structs into engine-native GuiButtonInit/GuiMenu.
 * @par Purpose:
 *     Bridges parsed JSON data to the engine's internal menu structures.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_MENUBUILDER_H
#define KFX_MENUBUILDER_H

#include "MenuLoader.h"

class MenuBuilder
{
public:
    /**
     * Build a GuiButtonInit array from a parsed MenuDefinition.
     * Terminated with sentinel {gbtype=-1}. Caller must free with FreeArray().
     */
    static struct GuiButtonInit* BuildButtonInitArray(const struct MenuDefinition *menuDef);

    /**
     * Build a GuiMenu struct from a parsed MenuDefinition and its button array.
     */
    static void BuildGuiMenu(const struct MenuDefinition *menuDef,
                             struct GuiButtonInit *buttons,
                             struct GuiMenu *menuOut);

    /** Free a GuiButtonInit array returned by BuildButtonInitArray(). */
    static void FreeArray(struct GuiButtonInit *buttons);
};

#endif /* KFX_MENUBUILDER_H */
