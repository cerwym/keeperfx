/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CallbackRegistry.h
 *     Maps callback name strings to C function pointers for JSON menu system.
 * @par Purpose:
 *     Provides a lookup table so JSON menu definitions can reference engine
 *     callbacks by name (e.g., "frontend_draw_large_menu_button").
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_CALLBACKREGISTRY_H
#define KFX_CALLBACKREGISTRY_H

#include "../../../bflib_guibtns.h"

class CallbackRegistry
{
public:
    static CallbackRegistry& GetInstance();

    /**
     * Resolve a callback name string to a C function pointer.
     * @param name The callback function name (e.g., "frontend_draw_large_menu_button")
     * @return The function pointer, or NULL if not found
     */
    Gf_Btn_Callback Resolve(const char *name) const;

private:
    CallbackRegistry() = default;

    struct Entry {
        const char *name;
        Gf_Btn_Callback func;
    };

    static const Entry s_table[];
};

#endif /* KFX_CALLBACKREGISTRY_H */
