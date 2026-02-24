/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuRegistry.cpp
 *     Runtime JSON menu registry implementation.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "../../../pre_inc.h"

#include "MenuRegistry.h"
#include "JsonParser.h"
#include "MenuBuilder.h"
#include "FreeformText.h"
#include "DynamicText.h"

#include "../../../bflib_basics.h"
#include "../../../bflib_fileio.h"
#include "../../../frontend.h"
#include "../../../frontmenu_select.h"
#include "../../../gui_frontmenu.h"
#include "../../../vidmode.h"

#include "../../../post_inc.h"

#include <string.h>
#include <stdio.h>

/******************************************************************************/

MenuRegistry& MenuRegistry::GetInstance()
{
    static MenuRegistry instance;
    return instance;
}

int MenuRegistry::FindMenu(const char *menuId) const
{
    for (int i = 0; i < m_menuCount; i++)
    {
        if (m_registry[i].loaded && strcmp(m_registry[i].menu_id, menuId) == 0)
            return i;
    }
    return -1;
}

int MenuRegistry::LoadAndRegister(const char *menuId)
{
    int existing = FindMenu(menuId);
    if (existing >= 0)
        return existing;

    if (m_menuCount >= MAX_JSON_MENUS)
    {
        ERRORLOG("JSON menu registry full (max %d), cannot load \"%s\"", MAX_JSON_MENUS, menuId);
        return -1;
    }

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "data/menus/%s.json", menuId);

    if (LbFileLength(filepath) <= 0)
    {
        WARNLOG("JSON menu file not found: \"%s\"", filepath);
        return -1;
    }

    int idx = m_menuCount;
    JsonMenuEntry *entry = &m_registry[idx];
    memset(entry, 0, sizeof(*entry));

    if (!JsonParser::GetInstance().LoadMenuFromJson(filepath, &entry->menu_def))
    {
        WARNLOG("Failed to parse JSON menu: \"%s\"", filepath);
        return -1;
    }

    strncpy(entry->menu_id, menuId, sizeof(entry->menu_id) - 1);
    entry->loaded = true;
    m_menuCount++;

    SYNCDBG(5, "Registered JSON menu \"%s\" at index %d (GMnu %d)", menuId, idx, GMNU_JSON_BASE + idx);
    return idx;
}

void MenuRegistry::DiscoverReferencedMenus(const struct MenuDefinition *menuDef)
{
    for (int i = 0; i < menuDef->button_count; i++)
    {
        const char *nav = menuDef->buttons[i].navigate_to;
        if (nav[0] != '\0')
        {
            int idx = LoadAndRegister(nav);
            if (idx >= 0 && !m_registry[idx].discovered)
            {
                m_registry[idx].discovered = true;
                DiscoverReferencedMenus(&m_registry[idx].menu_def);
            }
        }
    }
}

void MenuRegistry::ResolveNavigateTo(struct MenuDefinition *menuDef)
{
    for (int i = 0; i < menuDef->button_count; i++)
    {
        struct ButtonDefinition *btn = &menuDef->buttons[i];
        if (btn->navigate_to[0] != '\0')
        {
            int idx = FindMenu(btn->navigate_to);
            if (idx >= 0)
            {
                strncpy(btn->on_click.name, "frontend_navigate_json_menu", sizeof(btn->on_click.name) - 1);
                btn->on_click.is_lua = false;
                btn->btype_value = (unsigned short)idx;
            }
            else
            {
                WARNLOG("navigate_to target \"%s\" not found for button \"%s\"", btn->navigate_to, btn->id);
            }
        }
    }
}

void MenuRegistry::NavigateTo(int idx)
{
    if (idx < 0 || idx >= m_menuCount || !m_registry[idx].loaded)
    {
        ERRORLOG("Invalid JSON menu index %d", idx);
        return;
    }
    m_pendingIdx = idx;
    frontend_set_state(FeSt_JSON_MENU);
}

void MenuRegistry::NavigateToByName(const char *menuId)
{
    int idx = FindMenu(menuId);
    if (idx >= 0)
    {
        NavigateTo(idx);
    }
    else
    {
        ERRORLOG("JSON menu \"%s\" not found in registry", menuId);
    }
}

void MenuRegistry::SetupActiveMenu()
{
    if (m_pendingIdx >= 0 && m_pendingIdx < m_menuCount)
    {
        m_activeIdx = m_pendingIdx;
        turn_on_menu(GMNU_JSON_BASE + m_activeIdx);
        set_pointer_graphic_menu();
    }
}

void MenuRegistry::ShutdownActiveMenu()
{
    if (m_activeIdx >= 0 && m_activeIdx < m_menuCount)
    {
        turn_off_menu(GMNU_JSON_BASE + m_activeIdx);
    }
    m_activeIdx = -1;
}

void MenuRegistry::Init()
{
    if (m_initialized)
        return;
    m_initialized = true;

    /* Phase 1: Load main_menu.json and discover all referenced menus */
    JUSTLOG("MenuRegistry::Init Phase 1: Loading main_menu.json");
    const char *mainPath = "data/menus/main_menu.json";
    if (LbFileLength(mainPath) > 0)
    {
        if (JsonParser::GetInstance().LoadMenuFromJson(mainPath, &m_mainMenuDef))
        {
            m_mainMenuLoaded = true;
            JUSTLOG("MenuRegistry::Init: main_menu loaded, discovering references");
            DiscoverReferencedMenus(&m_mainMenuDef);
            JUSTLOG("MenuRegistry::Init: reference discovery complete, %d menus registered", m_menuCount);
        }
        else
        {
            WARNLOG("Failed to parse JSON menu: \"%s\"", mainPath);
        }
    }

    /* Discover menus referenced by already-registered menus (chains) */
    JUSTLOG("MenuRegistry::Init: discovering chain references");
    for (int i = 0; i < m_menuCount; i++)
    {
        if (m_registry[i].loaded)
            DiscoverReferencedMenus(&m_registry[i].menu_def);
    }

    /* Load additional menus not reachable via navigate_to chains */
    JUSTLOG("MenuRegistry::Init: loading additional menus");
    LoadAndRegister("campaign_hub");
    LoadAndRegister("global_load");
    LoadAndRegister("levelpack_menu");

    /* Initialize campaign list visibility for scrollable campaign menus */
    JUSTLOG("MenuRegistry::Init: loading campaign list");
    frontend_campaign_list_load();

    /* Phase 2: Resolve navigate_to references to registry indices */
    JUSTLOG("MenuRegistry::Init Phase 2: resolving navigate_to");
    if (m_mainMenuLoaded)
        ResolveNavigateTo(&m_mainMenuDef);

    for (int i = 0; i < m_menuCount; i++)
    {
        if (m_registry[i].loaded)
            ResolveNavigateTo(&m_registry[i].menu_def);
    }

    /* Phase 3: Build GuiButtonInit arrays and register with engine */
    JUSTLOG("MenuRegistry::Init Phase 3: building button arrays");
    if (m_mainMenuLoaded)
    {
        struct GuiButtonInit *buttons = MenuBuilder::BuildButtonInitArray(&m_mainMenuDef);
        if (buttons != NULL)
        {
            frontend_main_menu.buttons = buttons;
            m_mainMenuButtons = buttons;
            JUSTLOG("Loaded JSON menu override: main_menu (%d buttons)", m_mainMenuDef.button_count);
        }
        else
        {
            ERRORLOG("MenuRegistry::Init: failed to build main_menu buttons");
        }
    }

    for (int i = 0; i < m_menuCount; i++)
    {
        JsonMenuEntry *entry = &m_registry[i];
        if (!entry->loaded)
            continue;

        int gmnu_id = GMNU_JSON_BASE + i;
        if (gmnu_id >= MENU_LIST_ITEMS_COUNT)
        {
            ERRORLOG("MenuRegistry::Init: GMnu %d for \"%s\" exceeds menu_list capacity (%d), skipping",
                     gmnu_id, entry->menu_id, MENU_LIST_ITEMS_COUNT);
            continue;
        }

        JUSTLOG("MenuRegistry::Init: building buttons for \"%s\"", entry->menu_id);
        entry->buttons = MenuBuilder::BuildButtonInitArray(&entry->menu_def);
        if (entry->buttons == NULL)
        {
            ERRORLOG("Failed to build buttons for JSON menu \"%s\"", entry->menu_id);
            continue;
        }

        MenuBuilder::BuildGuiMenu(&entry->menu_def, entry->buttons, &entry->gui_menu);
        entry->gui_menu.ident = gmnu_id;
        menu_list[gmnu_id] = &entry->gui_menu;

        JUSTLOG("Registered JSON menu \"%s\" as GMnu %d (%d buttons)",
                entry->menu_id, gmnu_id, entry->menu_def.button_count);
    }

    JUSTLOG("MenuRegistry::Init complete: %d menus registered", m_menuCount);
}

void MenuRegistry::Shutdown()
{
    if (m_mainMenuButtons != NULL)
    {
        MenuBuilder::FreeArray(m_mainMenuButtons);
        m_mainMenuButtons = NULL;
    }
    for (int i = 0; i < m_menuCount; i++)
    {
        int gmnu_id = GMNU_JSON_BASE + i;
        if (gmnu_id < MENU_LIST_ITEMS_COUNT && m_registry[i].buttons != NULL)
        {
            menu_list[gmnu_id] = NULL;
            MenuBuilder::FreeArray(m_registry[i].buttons);
            m_registry[i].buttons = NULL;
        }
        m_registry[i].loaded = false;
        m_registry[i].discovered = false;
    }
    m_menuCount = 0;
    m_activeIdx = -1;
    m_pendingIdx = -1;
    m_mainMenuLoaded = false;
    FreeformText::GetInstance().Reset();
    DynamicText::GetInstance().Reset();
    m_initialized = false;
}
