/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file FreeformText.h
 *     Manages freeform text slots for JSON-defined menu buttons.
 * @par Purpose:
 *     Allows JSON menus to specify arbitrary text strings on buttons,
 *     bypassing the engine's frontend_button_info[] lookup system.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_FREEFORMTEXT_H
#define KFX_FREEFORMTEXT_H

#include "../../../bflib_guibtns.h"

#define MENU_MAX_FREETEXT 32
#define MENU_MAX_FREETEXT_LEN 256
#define MENU_FREETEXT_CONTENT_BASE 200

class FreeformText
{
public:
    static FreeformText& GetInstance();

    /**
     * Register a freeform text string. Returns the content.lval to use
     * (MENU_FREETEXT_CONTENT_BASE + slot), or -1 if full.
     */
    long Register(const char *text, unsigned char fontIndex);

    /** Get freeform text for a given content.lval. Returns NULL if not in range. */
    const char* GetText(long contentLval) const;

    /** Get the font index for a freeform text slot. */
    unsigned char GetFont(long contentLval) const;

    /** Draw callback for buttons with freeform text. */
    void DrawButton(struct GuiButton *gbtn) const;

    /** Reset all slots (called during shutdown). */
    void Reset();

private:
    FreeformText() = default;

    char m_texts[MENU_MAX_FREETEXT][MENU_MAX_FREETEXT_LEN] = {};
    unsigned char m_fonts[MENU_MAX_FREETEXT] = {};
    int m_count = 0;
};

#endif /* KFX_FREEFORMTEXT_H */
