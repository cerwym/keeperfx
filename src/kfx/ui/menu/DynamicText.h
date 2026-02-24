/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file DynamicText.h
 *     Runtime text providers for JSON menu buttons.
 * @par Purpose:
 *     Allows JSON menus to specify "dynamic_text": "provider_name" on buttons.
 *     At draw time, a registered C function is called to produce the text.
 *     Example: "active_campaign_name" returns the current campaign display name.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_DYNAMICTEXT_H
#define KFX_DYNAMICTEXT_H

#include "../../../bflib_guibtns.h"

#define MENU_MAX_DYNTEXT 32
#define MENU_DYNTEXT_CONTENT_BASE 300

/** Function type for dynamic text providers. Returns a string to display. */
typedef const char* (*DynTextProvider)(void);

class DynamicText
{
public:
    static DynamicText& GetInstance();

    /**
     * Register a dynamic text slot with a named provider.
     * @param providerName The provider name (e.g., "active_campaign_name")
     * @param fontIndex Font to use: 0=large, 1=normal, 2=column, 3=disabled
     * @return content.lval value (MENU_DYNTEXT_CONTENT_BASE + slot), or -1 if full/unknown
     */
    long Register(const char *providerName, unsigned char fontIndex);

    /** Get the current text for a dynamic text slot (calls provider). */
    const char* GetText(long contentLval) const;

    /** Get the font index for a dynamic text slot. */
    unsigned char GetFont(long contentLval) const;

    /** Draw callback for buttons with dynamic text. */
    void DrawButton(struct GuiButton *gbtn) const;

    /** Reset all slots (called during shutdown). */
    void Reset();

private:
    DynamicText() = default;

    struct ProviderEntry {
        const char *name;
        DynTextProvider func;
    };

    /** Resolve a provider name to a function pointer. */
    static DynTextProvider ResolveProvider(const char *name);

    static const ProviderEntry s_providers[];

    DynTextProvider m_slots[MENU_MAX_DYNTEXT] = {};
    unsigned char m_fonts[MENU_MAX_DYNTEXT] = {};
    int m_count = 0;
};

#endif /* KFX_DYNAMICTEXT_H */
