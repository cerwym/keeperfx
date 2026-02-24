/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file MenuLoader.h
 *     JSON menu loader and registry for data-driven UI menus.
 * @par Purpose:
 *     Loads menu definitions from JSON files, builds engine-compatible
 *     GuiButtonInit/GuiMenu structures, and manages a registry of
 *     dynamically-loaded menus with navigate_to resolution.
 * @par Comment:
 *     Replaces hardcoded C menu arrays with JSON-driven definitions.
 *     Uses CentiJSON for parsing. Provides extern "C" interface for
 *     integration with the existing C frontend engine.
 * @author   Peter Lockett, KeeperFX Team
 * @date     23 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef KFX_MENULOADER_H
#define KFX_MENULOADER_H

#include "../../../pre_inc.h"

#include "../../../globals.h"
#include "../../../bflib_basics.h"
#include "../../../bflib_guibtns.h"

#include "../../../post_inc.h"

#define MENU_MAX_BUTTONS 32
#define MENU_MAX_ANIM_FRAMES 16
#define MENU_MAX_SPRITES_PER_FRAME 6
#define MENU_MAX_CALLBACK_NAME 128
#define MENU_MAX_LUA_SCRIPTS 8
#define MENU_MAX_ID_LEN 64
#define MENU_MAX_FREETEXT 32
#define MENU_MAX_FREETEXT_LEN 256
#define MENU_FREETEXT_CONTENT_BASE 200  /** content.lval offset for freeform text slots */
#define MAX_JSON_MENUS 16
#define GMNU_JSON_BASE 49  /** First GMnu_ ID for dynamically loaded JSON menus */

#ifdef __cplusplus
extern "C" {
#endif

/** Sprite composition patterns for UI elements */
enum SpriteComposition {
    SprComp_None = 0,
    SprComp_Single,        /** Single sprite */
    SprComp_ThreePiece,    /** Left + Center(tiled) + Right */
    SprComp_FourPiece,     /** Left + Center + Center + Right */
    SprComp_NineSlice,     /** 3x3 grid: corners + edges + center */
};

/** A single animation frame containing sprite indices */
struct SpriteFrame {
    short sprites[MENU_MAX_SPRITES_PER_FRAME];
    int sprite_count;
};

/** Visual state for a button (default, hover, disabled, pressed) */
struct VisualState {
    struct SpriteFrame frames[MENU_MAX_ANIM_FRAMES];
    int frame_count;
    int frame_duration_ms;
    unsigned char font_index;
};

/** Complete visual definition for a button */
struct VisualDefinition {
    enum SpriteComposition composition;
    struct VisualState state_default;
    struct VisualState state_hover;
    struct VisualState state_disabled;
    struct VisualState state_pressed;
};

/** Callback definition — either a named C function or a Lua script string */
struct CallbackDefinition {
    char name[MENU_MAX_CALLBACK_NAME];
    TbBool is_lua;
};

/** Definition of a single button parsed from JSON */
struct ButtonDefinition {
    char id[MENU_MAX_ID_LEN];
    enum TbButtonType type;
    short id_num;              /** BID_ value (e.g., BID_MENU_TITLE). 0 = BID_DEFAULT */
    unsigned short btype_value; /** Type-specific value. For frontend_change_state: target FeSt_ state */
    int pos_x, pos_y;
    int width, height;
    struct VisualDefinition visual;
    TextStringId text_string_id;
    TbBool enabled, visible, clickable;
    struct CallbackDefinition on_click;
    struct CallbackDefinition on_rclick;
    struct CallbackDefinition on_hover;
    struct CallbackDefinition on_draw;
    struct CallbackDefinition on_maintain;
    long content_value;
    char content_name[MENU_MAX_CALLBACK_NAME]; /** Symbolic name for content_value lookup */
    char text[MENU_MAX_FREETEXT_LEN];          /** Freeform text — overrides content_value text lookup */
    unsigned char font_index;                  /** Font for freeform text: 0=large, 1=normal, 2=column, 3=disabled */
    char dynamic_text[MENU_MAX_CALLBACK_NAME]; /** Named text provider called each frame (e.g. "active_campaign_name") */
    char navigate_to[MENU_MAX_ID_LEN];         /** Target JSON menu ID — auto-sets on_click and btype_value */
    short maxval;
    short tooltip_stridx;
};

/** Definition of an entire menu parsed from JSON */
struct MenuDefinition {
    char menu_id[MENU_MAX_ID_LEN];
    char description[256];
    TbBool position_centered;
    int pos_x, pos_y;
    int width, height;
    TbBool is_monopoly_menu;    /** Modal menus block interaction with menus behind them */
    struct CallbackDefinition draw_cb;
    struct CallbackDefinition create_cb;
    struct ButtonDefinition buttons[MENU_MAX_BUTTONS];
    int button_count;
    char lua_scripts[MENU_MAX_LUA_SCRIPTS][256];
    int lua_script_count;
};

/**
 * Load and parse a JSON menu definition file.
 * @param filepath Path to the JSON file
 * @param menu_def Output: parsed menu definition (caller-owned, use free_menu_definition to clean up)
 * @return true on success, false on parse error
 */
TbBool load_menu_from_json(const char *filepath, struct MenuDefinition *menu_def);

/**
 * Build a GuiButtonInit array from a parsed MenuDefinition.
 * The returned array is dynamically allocated and terminated with a {-1, ...} sentinel.
 * Caller must free the returned array with free_button_init_array().
 * @param menu_def The parsed menu definition
 * @return Dynamically allocated GuiButtonInit array, or NULL on failure
 */
struct GuiButtonInit *build_button_init_array(const struct MenuDefinition *menu_def);

/**
 * Build a GuiMenu struct from a parsed MenuDefinition and its button array.
 * @param menu_def The parsed menu definition
 * @param buttons The GuiButtonInit array (from build_button_init_array)
 * @param menu_out Output: filled GuiMenu struct
 */
void build_gui_menu(const struct MenuDefinition *menu_def, struct GuiButtonInit *buttons, struct GuiMenu *menu_out);

/**
 * Free a GuiButtonInit array returned by build_button_init_array().
 */
void free_button_init_array(struct GuiButtonInit *buttons);

/**
 * Resolve a callback name string to a C function pointer.
 * @param name The callback function name (e.g., "frontend_draw_large_menu_button")
 * @return The function pointer, or NULL if not found
 */
Gf_Btn_Callback resolve_button_callback(const char *name);

/**
 * Try to load JSON menu overrides from data/menus/.
 * If a JSON file exists for a menu, the corresponding GuiMenu's button array
 * is replaced with the JSON-loaded version. Safe to call multiple times
 * (only loads once). Falls back to hardcoded C arrays if files are missing.
 */
void init_json_menus(void);

/**
 * Free any dynamically allocated JSON menu data.
 * Call during shutdown.
 */
void shutdown_json_menus(void);

/**
 * Navigate callback for JSON menu buttons with navigate_to targets.
 * Reads btype_value as JSON menu registry index, transitions to FeSt_JSON_MENU.
 */
void frontend_navigate_json_menu(struct GuiButton *gbtn);

/**
 * Setup the active JSON menu (called from frontend_setup_state FeSt_JSON_MENU case).
 */
void json_menu_setup(void);

/**
 * Shutdown the active JSON menu (called from frontend_shutdown_state FeSt_JSON_MENU case).
 */
void json_menu_shutdown(void);

/**
 * Draw callback for buttons using freeform text from JSON.
 * Renders the same large 3-piece button but draws text from freeform storage.
 */
void frontend_draw_large_freetext_button(struct GuiButton *gbtn);

/**
 * Draw callback for buttons using dynamic text providers.
 * Calls a registered text provider function each frame to get the display text.
 */
void frontend_draw_large_dyntext_button(struct GuiButton *gbtn);

/**
 * Campaign selection callback — reads btype_value as index into campaigns_list,
 * calls change_campaign(), then navigates to campaign_hub JSON menu.
 */
void frontend_select_campaign(struct GuiButton *gbtn);

/**
 * Campaign list click callback — reads content.lval as campaign slot (45-51),
 * combines with scroll offset to select campaign, then navigates to campaign_hub.
 */
void frontend_campaign_select_to_hub(struct GuiButton *gbtn);

/**
 * Start the currently selected campaign (campaign.fname) as a new game.
 * Transitions to FeSt_CAMPAIGN_INTRO → land view.
 */
void frontend_start_selected_campaign(struct GuiButton *gbtn);

/**
 * Navigate to a JSON menu by its menu_id string.
 */
void navigate_to_json_menu_by_name(const char *menuId);

/**
 * Draw callback for campaign list rows showing name + crown icon + level count.
 * Replaces frontend_draw_campaign_select_button with enriched display.
 */
void frontend_draw_campaign_row(struct GuiButton *gbtn);

/**
 * Click callback for campaign hub High Scores button.
 * Sets json menu return flag and navigates to FeSt_HIGH_SCORES.
 */
void frontend_hub_high_scores(struct GuiButton *gbtn);

/**
 * Get the freeform text string for a given content.lval value.
 * Returns NULL if not a freeform text slot.
 */
const char *get_freeform_text(long content_lval);

/** Navigate to the global load game browser. Scans all saves, then shows global_load menu. */
void frontend_navigate_to_global_load(struct GuiButton *gbtn);
/** Navigate to the campaign-specific load game browser. Scans only the active campaign's saves. */
void frontend_navigate_to_campaign_load(struct GuiButton *gbtn);
/** Draw a single row in the global load game browser. */
void frontend_draw_global_save_row(struct GuiButton *gbtn);
/** Click handler for a global load game browser row. */
void frontend_global_load_click(struct GuiButton *gbtn);
/** Maintain callback — enables/disables row based on valid save entry. */
void frontend_global_load_maintain(struct GuiButton *gbtn);
/** Scroll up in global load browser. */
void frontend_global_load_up(struct GuiButton *gbtn);
void frontend_global_load_up_maintain(struct GuiButton *gbtn);
/** Scroll down in global load browser. */
void frontend_global_load_down(struct GuiButton *gbtn);
void frontend_global_load_down_maintain(struct GuiButton *gbtn);
/** Scroll tab drag in global load browser. */
void frontend_global_load_scroll(struct GuiButton *gbtn);
void frontend_draw_global_load_scroll_tab(struct GuiButton *gbtn);

#ifdef __cplusplus
}
#endif

#endif /* KFX_MENULOADER_H */
