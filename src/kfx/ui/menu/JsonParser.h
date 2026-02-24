/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file JsonParser.h
 *     Parses JSON menu definition files into MenuDefinition structures.
 * @par Purpose:
 *     Handles all JSON parsing logic: file I/O, CentiJSON DOM traversal,
 *     and conversion to internal MenuDefinition/ButtonDefinition structs.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_JSONPARSER_H
#define KFX_JSONPARSER_H

#include "MenuLoader.h"

class JsonParser
{
public:
    static JsonParser& GetInstance();

    /**
     * Load and parse a JSON menu definition file.
     * @param filepath Path to the JSON file
     * @param menuDef Output: parsed menu definition
     * @return true on success, false on parse error
     */
    TbBool LoadMenuFromJson(const char *filepath, struct MenuDefinition *menuDef) const;

    /** Resolve a content name string to a frontend_button_info[] index. */
    static long ResolveContentName(const char *name);

private:
    JsonParser() = default;

    struct ContentNameEntry {
        const char *name;
        long value;
    };
    static const ContentNameEntry s_contentNames[];
};

#endif /* KFX_JSONPARSER_H */
