/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuRegistry.h
 *     Runtime registry for dynamically-loaded JSON menus.
 * @par Purpose:
 *     Manages loading, discovery, lifecycle, and navigation of JSON menus.
 *     Provides Init/Shutdown/Setup/Navigate entry points for the frontend.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_MENUREGISTRY_H
#define KFX_MENUREGISTRY_H

#include "MenuLoader.h"

class MenuRegistry
{
public:
    static MenuRegistry& GetInstance();

    /** Initialize JSON menus: load, discover, resolve references, build. */
    void Init();

    /** Free all dynamically allocated menu data. */
    void Shutdown();

    /** Setup the active JSON menu (call from FeSt_JSON_MENU setup). */
    void SetupActiveMenu();

    /** Shutdown the active JSON menu (call from FeSt_JSON_MENU shutdown). */
    void ShutdownActiveMenu();

    /** Navigate to a JSON menu by registry index. Sets pending + transitions state. */
    void NavigateTo(int idx);

    /** Navigate to a JSON menu by menu_id string. */
    void NavigateToByName(const char *menuId);

private:
    MenuRegistry() = default;

    struct JsonMenuEntry {
        char menu_id[MENU_MAX_ID_LEN];
        struct GuiMenu gui_menu;
        struct GuiButtonInit *buttons;
        struct MenuDefinition menu_def;
        TbBool loaded;
    };

    int FindMenu(const char *menuId) const;
    int LoadAndRegister(const char *menuId);
    void DiscoverReferencedMenus(const struct MenuDefinition *menuDef);
    void ResolveNavigateTo(struct MenuDefinition *menuDef);

    JsonMenuEntry m_registry[MAX_JSON_MENUS] = {};
    int m_menuCount = 0;
    int m_activeIdx = -1;
    int m_pendingIdx = -1;

    struct GuiButtonInit *m_mainMenuButtons = nullptr;
    struct MenuDefinition m_mainMenuDef = {};
    bool m_mainMenuLoaded = false;
    bool m_initialized = false;
};

#endif /* KFX_MENUREGISTRY_H */
