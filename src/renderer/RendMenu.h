/******************************************************************************/
// Dungeon Keeper - Renderer Settings Menu
/******************************************************************************/
/** @file RendMenu.h
 *     Entry definitions for the standalone renderer settings overlay.
 *
 * @par Design:
 *     The menu is a scrollable list of typed rows.  Each row carries:
 *       - the widget kind (section header, toggle, cycle, slider)
 *       - a direct pointer into g_renderer_settings for the field it controls
 *       - a display label and a one-line description
 *       - an optional availability predicate (row dimmed/non-interactive when 0)
 *
 *     There is no dependency on GuiMenu, GuiButton, or any part of the legacy
 *     Bullfrog frontend.  This is a self-contained overlay system.
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Widget kinds
 * ------------------------------------------------------------------------- */
typedef enum RendMenuWidgetKind {
    /** Non-interactive section header / divider row.  Only `label` is used. */
    RMENU_WIDGET_SECTION,

    /** Non-interactive label / divider row within a tab.
     *  Like SECTION it uses only `label`, but unlike SECTION it does NOT
     *  create a new tab.  Navigation automatically skips these rows. */
    RMENU_WIDGET_LABEL,

    /** int*   field — toggled between 0 and 1 on activation. */
    RMENU_WIDGET_TOGGLE,

    /** float* field — toggled between 0.0f and 1.0f on activation.
     *  Used for fields that are conceptually boolean but stored as float
     *  (e.g. shade_fullbright) so no float-through-int-pointer aliasing is needed. */
    RMENU_WIDGET_TOGGLE_F,

    /** int*   field — cycled through a named option list on left/right/activate.
     *  Wraps around at both ends. */
    RMENU_WIDGET_CYCLE,

    /** float* field — incremented/decremented by step_f on left/right,
     *  clamped to [min_f, max_f]. */
    RMENU_WIDGET_SLIDER_F,

    /** int*   field — incremented/decremented by 1 on left/right,
     *  clamped to [min_i, max_i]. */
    RMENU_WIDGET_SLIDER_I,
} RendMenuWidgetKind;

/* ---------------------------------------------------------------------------
 * Entry struct
 * ------------------------------------------------------------------------- */

/** A single row in the renderer settings menu. */
typedef struct RendMenuEntry {
    RendMenuWidgetKind kind;

    /** Display name.  NULL marks the sentinel end of the table. */
    const char* label;

    /** One-line description shown at the bottom of the screen when this row
     *  is focused.  May be NULL for section rows. */
    const char* desc;

    /** Optional availability predicate.  If non-NULL and returns 0 the row is
     *  rendered dimmed and cannot receive keyboard/mouse focus.
     *  SECTION rows ignore this field entirely. */
    int (*is_enabled)(void);

    /** Per-widget data.  Only the union member matching `kind` is valid. */
    union {
        /** RMENU_WIDGET_TOGGLE */
        struct { int*   val; } toggle;

        /** RMENU_WIDGET_TOGGLE_F */
        struct { float* val; } toggle_f;

        /** RMENU_WIDGET_CYCLE */
        struct {
            int*         val;
            /** Pointer to a static const char* array of option labels. */
            const char** opts;
            int          num_opts;
        } cycle;

        /** RMENU_WIDGET_SLIDER_F */
        struct {
            float* val;
            float  min_f;
            float  max_f;
            float  step_f;
        } slider_f;

        /** RMENU_WIDGET_SLIDER_I */
        struct {
            int* val;
            int  min_i;
            int  max_i;
        } slider_i;
    } w;

} RendMenuEntry;

/* ---------------------------------------------------------------------------
 * Table accessor + tab descriptor
 * ------------------------------------------------------------------------- */

/** Returns a pointer to the static entry table.
 *  *out_count receives the number of entries (not counting the NULL sentinel).
 *  The returned pointer is valid for the lifetime of the program. */
const RendMenuEntry* RendMenu_GetEntries(int* out_count);

/** A tab, derived directly from one SECTION row in the entry table.
 *  All entries between this SECTION and the next belong to this tab. */
typedef struct {
    const char* name;            /**< Section label used as the tab title.   */
    int         first_entry_idx; /**< Index in GetEntries() of the first
                                  *   non-section entry in this tab.          */
    int         count;           /**< Number of entries (all interactive).    */
} RendMenuTab;

/** Returns the pre-computed tab array.
 *  *out_count receives the number of tabs.
 *  Computed once on first call from the entry table. */
const RendMenuTab* RendMenu_GetTabs(int* out_count);

#ifdef __cplusplus
}
#endif
