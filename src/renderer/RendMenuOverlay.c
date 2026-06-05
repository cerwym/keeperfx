/******************************************************************************/
// Dungeon Keeper - Renderer Settings Menu
/******************************************************************************/
/** @file RendMenuOverlay.c
 *     State machine, input handling and drawing for the renderer settings
 *     overlay.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendMenuOverlay.h"
#include "renderer/RendMenu.h"
#include "renderer/RendererSettings.h"
#include "renderer/RendererManager.h"
#include "bflib_sprfnt.h"
#include "bflib_keybrd.h"
#include "bflib_datetm.h"      /* LbTimerClock, TbClockMSec */
#include "frontend.h"          /* winfont, frontend_font */
#include "game_legacy.h"       /* game.operation_flags, GOF_Paused */
#include "config_keeperfx.h"   /* cfg_renderer_menu_pause */
#include <stdio.h>
#include <string.h>
#include "post_inc.h"

/* ---------------------------------------------------------------------------
 * Text style system
 *
 * Each visual role in the menu has a named style.  Styles encode:
 *   font_ptr   — pointer to the TbSpriteSheet* global to use
 *   ups_pct    — scale relative to the computed base ups (100 = 1:1)
 *   draw_flags — OR'd into lbDisplay.DrawFlags for this draw
 *
 * Color comes from the font sprite sheet itself, not DrawColour:
 *   frontend_font[0] — dim     (face color 238)  inactive / unimportant
 *   frontend_font[1] — normal  (face color 243)  default label
 *   frontend_font[2] — bright  (face color 248)  focused / active
 *   frontend_font[3] — accent  (face color 119)  value strings
 * ------------------------------------------------------------------------- */

typedef struct {
    struct TbSpriteSheet** font_ptr; /* &winfont, &frontend_font[N], etc. */
    int           ups_pct;           /* 100 = base size, 125 = 25% larger  */
    unsigned short draw_flags;
} RendMenuTextStyle;

/* Roles — add new ones here as the menu grows */
typedef enum {
    RMSTYLE_TAB_INACTIVE = 0, /* tab not currently selected        */
    RMSTYLE_TAB_ACTIVE,       /* currently selected tab            */
    RMSTYLE_LABEL,            /* normal interactive row label      */
    RMSTYLE_LABEL_FOCUSED,    /* label on the focused row          */
    RMSTYLE_LABEL_DISABLED,   /* label on a greyed-out row         */
    RMSTYLE_VALUE,            /* right-side value string           */
    RMSTYLE_VALUE_FOCUSED,    /* value string on focused row       */
    RMSTYLE_CURSOR,           /* the > glyph beside focused row    */
    RMSTYLE_ARROW,            /* < > navigation arrows on values   */
    RMSTYLE_DESC,             /* description bar hint text         */
    RMSTYLE_NAV_ARROW,        /* tab bar < > scroll arrows         */
    RMSTYLE_COUNT
} RendMenuStyleId;

/* The style table — indexed by RendMenuStyleId.
 *
 * NOTE: frontend_font[0..3] are only resident during frontend screens.
 * During gameplay only winfont is guaranteed to be loaded, so all in-game
 * styles use winfont.  Visual differentiation is achieved through:
 *   ups_pct      — size relative to base (100 = normal, 110 = 10% larger)
 *   draw_flags   — TRANSPAR4 = light dim (~25%), TRANSPAR8 = heavy dim (~50%)
 */
static const RendMenuTextStyle k_styles[RMSTYLE_COUNT] = {
    /* RMSTYLE_TAB_INACTIVE */ { &winfont,  95, 0                   },
    /* RMSTYLE_TAB_ACTIVE   */ { &winfont, 110, 0                   },
    /* RMSTYLE_LABEL        */ { &winfont, 100, 0                   },
    /* RMSTYLE_LABEL_FOCUSED*/ { &winfont, 100, 0                   },
    /* RMSTYLE_LABEL_DISABL */ { &winfont, 100, Lb_SPRITE_TRANSPAR8 },
    /* RMSTYLE_VALUE        */ { &winfont, 100, 0                   },
    /* RMSTYLE_VALUE_FOCUSED*/ { &winfont, 100, 0                   },
    /* RMSTYLE_CURSOR       */ { &winfont, 100, 0                   },
    /* RMSTYLE_ARROW        */ { &winfont, 100, 0                   },
    /* RMSTYLE_DESC         */ { &winfont, 100, 0                   },
    /* RMSTYLE_NAV_ARROW    */ { &winfont,  95, 0                   },
};

/**
 * Apply a named style and return the effective units_per_px to pass to
 * LbTextDrawResized.  Call this immediately before every text draw.
 * Guards against a NULL font pointer (e.g. frontend fonts not loaded).
 */
static int style_begin(RendMenuStyleId id, int base_ups)
{
    const RendMenuTextStyle* s = &k_styles[id];
    struct TbSpriteSheet* fnt = (s->font_ptr && *s->font_ptr) ? *s->font_ptr : winfont;
    LbTextSetFont(fnt);
    lbDisplay.DrawFlags = s->draw_flags;
    return base_ups * s->ups_pct / 100;
}

/* ---------------------------------------------------------------------------
 * Preview animation constants
 * Mirrors Ironwail's settings-preview behaviour: when a value changes the
 * full panel fades out and a compact bar fades in, holds, then reverses.
 * All times are in milliseconds (wall-clock via LbTimerClock).
 * ------------------------------------------------------------------------- */
#define PREVIEW_FADEIN_MS  125.0f   /* fade-in / fade-out duration */
#define PREVIEW_HOLD_MS   1250.0f   /* hold duration at peak       */

static float ease_in_out(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

typedef struct {
    int is_open;
    int current_tab;     /* index into RendMenu_GetTabs()                   */
    int focused_in_tab;  /* 0-based index within current tab                */
    int scroll_top;      /* first visible entry within current tab          */
    int tab_scroll;      /* index of leftmost visible tab in the tab bar    */
    int paused_game;     /* 1 if we set GOF_Paused on open                  */

    /* Preview animation (see PREVIEW_* constants above) */
    float        preview_frac;       /* 0.0=full panel, 1.0=compact bar only  */
    float        preview_hold_ms;    /* remaining hold time in ms             */
    int          preview_entry_abs;  /* absolute entry index being previewed  */
    TbClockMSec  preview_tick_ms;    /* wall-clock ms at last draw, 0=unset   */
} RendMenuState;

static RendMenuState s_state;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/** Return the active entry (absolute index) for the current state. */
static const RendMenuEntry* get_focused_entry(void)
{
    int tc = 0;
    const RendMenuTab*   tabs    = RendMenu_GetTabs(&tc);
    int ec = 0;
    const RendMenuEntry* entries = RendMenu_GetEntries(&ec);
    if (s_state.current_tab >= tc) return &entries[0];
    const RendMenuTab* tab = &tabs[s_state.current_tab];
    int abs_idx = tab->first_entry_idx + s_state.focused_in_tab;
    if (abs_idx >= ec) abs_idx = ec - 1;
    return &entries[abs_idx];
}

/** Clamp focused_in_tab to the current tab's valid range. */
static void clamp_focus(void)
{
    int tc = 0;
    const RendMenuTab* tabs = RendMenu_GetTabs(&tc);
    if (s_state.current_tab >= tc) s_state.current_tab = tc - 1;
    int count = tabs[s_state.current_tab].count;
    if (count <= 0) { s_state.focused_in_tab = 0; return; }
    if (s_state.focused_in_tab < 0)          s_state.focused_in_tab = 0;
    if (s_state.focused_in_tab >= count)      s_state.focused_in_tab = count - 1;
}

/** Scroll so focused_in_tab is always within the visible window. */
static void scroll_to_focused(int visible_rows)
{
    if (s_state.focused_in_tab < s_state.scroll_top)
        s_state.scroll_top = s_state.focused_in_tab;
    else if (s_state.focused_in_tab >= s_state.scroll_top + visible_rows)
        s_state.scroll_top = s_state.focused_in_tab - visible_rows + 1;
    if (s_state.scroll_top < 0) s_state.scroll_top = 0;
}

/** Switch to tab index t, resetting focus and scroll. */
static void switch_tab(int t)
{
    int tc = 0;
    RendMenu_GetTabs(&tc);
    if (tc <= 0) return;
    if (t < 0)    t = tc - 1;
    if (t >= tc)  t = 0;
    s_state.current_tab    = t;
    s_state.focused_in_tab = 0;
    s_state.scroll_top     = 0;
}

/* ---------------------------------------------------------------------------
 * Widget value helpers
 * ------------------------------------------------------------------------- */

static void entry_adjust(const RendMenuEntry* e, int dir)
{
    switch (e->kind)
    {
    case RMENU_WIDGET_TOGGLE:
        *e->w.toggle.val = (*e->w.toggle.val == 0) ? 1 : 0;
        break;

    case RMENU_WIDGET_TOGGLE_F:
        *e->w.toggle_f.val = (*e->w.toggle_f.val < 0.5f) ? 1.0f : 0.0f;
        break;

    case RMENU_WIDGET_CYCLE:
        {
            int v = *e->w.cycle.val + dir;
            if (v < 0)              v = e->w.cycle.num_opts - 1;
            if (v >= e->w.cycle.num_opts) v = 0;
            *e->w.cycle.val = v;
        }
        break;

    case RMENU_WIDGET_SLIDER_F:
        {
            float step  = (dir > 0) ?  e->w.slider_f.step_f
                                     : -e->w.slider_f.step_f;
            float v     = *e->w.slider_f.val + step;
            if (v < e->w.slider_f.min_f) v = e->w.slider_f.min_f;
            if (v > e->w.slider_f.max_f) v = e->w.slider_f.max_f;
            *e->w.slider_f.val = v;
        }
        break;

    default:
        break;
    }

    RendererApplySettings(&g_renderer_settings);
}

/** Format the current value of an entry as a short string. */
static void entry_value_str(const RendMenuEntry* e, char* buf, int buflen)
{
    switch (e->kind)
    {
    case RMENU_WIDGET_TOGGLE:
        snprintf(buf, buflen, "[%s]", *e->w.toggle.val ? "ON" : "OFF");
        break;

    case RMENU_WIDGET_TOGGLE_F:
        snprintf(buf, buflen, "[%s]", (*e->w.toggle_f.val >= 0.5f) ? "ON" : "OFF");
        break;

    case RMENU_WIDGET_CYCLE:
        {
            int v = *e->w.cycle.val;
            if (v >= 0 && v < e->w.cycle.num_opts)
                snprintf(buf, buflen, "%s", e->w.cycle.opts[v]);
            else
                snprintf(buf, buflen, "?");
        }
        break;

    case RMENU_WIDGET_SLIDER_F:
        {
            float v   = *e->w.slider_f.val;
            float min = e->w.slider_f.min_f;
            float max = e->w.slider_f.max_f;
            /* 8-char ASCII bar */
            int filled = (int)(7.0f * (v - min) / (max - min + 1e-6f) + 0.5f);
            char bar[10];
            int i;
            for (i = 0; i < 7; i++)
                bar[i] = (i < filled) ? '=' : '-';
            bar[7] = '\0';
            snprintf(buf, buflen, "%.2f [%s]", v, bar);
        }
        break;

    default:
        buf[0] = '\0';
        break;
    }
}

/** Trigger the compact preview bar for the currently focused entry. */
static void preview_set(void)
{
    const RendMenuEntry* e = get_focused_entry();
    int has_value = (e->kind == RMENU_WIDGET_TOGGLE   ||
                     e->kind == RMENU_WIDGET_TOGGLE_F ||
                     e->kind == RMENU_WIDGET_CYCLE     ||
                     e->kind == RMENU_WIDGET_SLIDER_F);
    if (!has_value)
        return;
    int tc = 0;
    const RendMenuTab* tabs = RendMenu_GetTabs(&tc);
    if (s_state.current_tab < tc)
        s_state.preview_entry_abs = tabs[s_state.current_tab].first_entry_idx
                                  + s_state.focused_in_tab;
    s_state.preview_hold_ms = PREVIEW_HOLD_MS;
}

/* ---------------------------------------------------------------------------
 * Public API — state machine
 * ------------------------------------------------------------------------- */

void RendMenu_Open(void)
{
    if (s_state.is_open)
        return;
    s_state.is_open        = 1;
    s_state.current_tab    = 0;
    s_state.focused_in_tab = 0;
    s_state.scroll_top     = 0;
    s_state.tab_scroll     = 0;
    s_state.paused_game    = 0;
    s_state.preview_frac      = 0.0f;
    s_state.preview_hold_ms   = 0.0f;
    s_state.preview_entry_abs = 0;
    s_state.preview_tick_ms   = 0;

    if (cfg_renderer_menu_pause &&
        (game.operation_flags & GOF_Paused) == 0)
    {
        set_flag(game.operation_flags, GOF_Paused);
        s_state.paused_game = 1;
    }
}

void RendMenu_Close(void)
{
    if (!s_state.is_open)
        return;
    s_state.is_open = 0;
    s_state.preview_frac    = 0.0f;
    s_state.preview_hold_ms = 0.0f;
    s_state.preview_tick_ms = 0;

    if (s_state.paused_game)
    {
        clear_flag(game.operation_flags, GOF_Paused);
        s_state.paused_game = 0;
    }

    RendererSettings_Save();
}

void RendMenu_ToggleOpen(void)
{
    if (s_state.is_open) RendMenu_Close();
    else                 RendMenu_Open();
}

int RendMenu_IsOpen(void) { return s_state.is_open; }

int RendMenu_HandleKey(int kc)
{
    if (!s_state.is_open)
        return 0;

    int tc = 0;
    const RendMenuTab* tabs = RendMenu_GetTabs(&tc);
    const RendMenuEntry* focused = get_focused_entry();

    switch (kc)
    {
    case KC_ESCAPE:
        RendMenu_Close();
        return 1;

    case KC_TAB:
        switch_tab(s_state.current_tab + 1);
        return 1;

    case KC_UP:
        s_state.focused_in_tab--;
        if (s_state.focused_in_tab < 0)
        {
            /* Wrap to previous tab, last entry */
            switch_tab(s_state.current_tab - 1);
            int new_count = tabs[s_state.current_tab].count;
            s_state.focused_in_tab = (new_count > 0) ? new_count - 1 : 0;
        }
        scroll_to_focused(24);
        return 1;

    case KC_DOWN:
        s_state.focused_in_tab++;
        if (s_state.focused_in_tab >= tabs[s_state.current_tab].count)
        {
            /* Wrap to next tab, first entry */
            switch_tab(s_state.current_tab + 1);
        }
        scroll_to_focused(24);
        return 1;

    case KC_LEFT:
        if (focused->is_enabled == NULL || focused->is_enabled()) {
            entry_adjust(focused, -1);
            preview_set();
        }
        return 1;

    case KC_RIGHT:
    case KC_RETURN:
        if (focused->is_enabled == NULL || focused->is_enabled()) {
            entry_adjust(focused, +1);
            preview_set();
        }
        return 1;

    default:
        return 1;
    }
}

/* ---------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */

void RendMenu_Draw(void)
{
    if (!s_state.is_open)
        return;

    /* --- Save all text rendering state we will touch --- */
    const struct TbSpriteSheet* save_font = TextRenderer_GetFont();
    int save_cx, save_cy, save_cw, save_ch;
    int save_jx, save_jy, save_jw;
    unsigned short save_draw_flags = lbDisplay.DrawFlags;
    LbTextGetClipWindow(&save_cx, &save_cy, &save_cw, &save_ch);
    LbTextGetJustifyWindow(&save_jx, &save_jy, &save_jw);

    int sw = (int)RendererGetScreenWidth();
    int sh = (int)RendererGetScreenHeight();

    /* Establish a known full-screen window baseline for all our draws.
       LbTextDrawResized coordinates are relative to the window origin,
       so we must control this explicitly throughout. */
    LbTextSetWindow(0, 0, sw, sh);

    /* Base scale: 16 units = 1px at 400-line reference height */
    int ups = 16 * sh / 400;
    if (ups < 8)  ups = 8;
    if (ups > 48) ups = 48;

    /* Measure line height using the normal-label font as reference */
    LbTextSetFont(*k_styles[RMSTYLE_LABEL].font_ptr);
    int lh    = LbTextLineHeight() * ups / 16;
    int row_h = lh + 3 * ups / 16;

    /* Full-screen panel — no margins */
    int mx = 0;
    int my = 0;
    int pw = sw;
    int ph = sh;

    int tab_bar_h = lh + 8 * ups / 16;
    int desc_h    = lh + 6 * ups / 16;
    int rows_y    = my + tab_bar_h;
    int rows_h    = ph - tab_bar_h - desc_h;
    int vis       = (rows_h > 0 && row_h > 0) ? rows_h / row_h : 1;

    scroll_to_focused(vis);

    int tc = 0;
    const RendMenuTab*   tabs    = RendMenu_GetTabs(&tc);
    int ec = 0;
    const RendMenuEntry* entries = RendMenu_GetEntries(&ec);

    /* --- Full-screen dark background --- */
    UIRenderer_SubmitSolidBoxAlpha(0, 0, sw, sh, 0, 0.88f);

    /* --- Tab bar --- */
    UIRenderer_SubmitSolidBoxAlpha(0, 0, sw, tab_bar_h, 0, 0.55f);

    /* Compute how many tabs fit: reserve room for < / > arrows */
    int arrow_w     = lh + 4 * ups / 16;   /* width of one arrow glyph + padding */
    int min_tab_w   = lh * 4;              /* minimum slot width                  */
    int bar_inner_w = sw - 2 * arrow_w;
    int tabs_vis    = (min_tab_w > 0) ? bar_inner_w / min_tab_w : 1;
    if (tabs_vis < 1)  tabs_vis = 1;
    if (tabs_vis > tc) tabs_vis = tc;

    /* Slide tab_scroll so current_tab is always visible */
    if (s_state.current_tab < s_state.tab_scroll)
        s_state.tab_scroll = s_state.current_tab;
    if (s_state.current_tab >= s_state.tab_scroll + tabs_vis)
        s_state.tab_scroll = s_state.current_tab - tabs_vis + 1;
    if (s_state.tab_scroll < 0) s_state.tab_scroll = 0;

    int show_left  = (s_state.tab_scroll > 0);
    int show_right = (s_state.tab_scroll + tabs_vis < tc);
    int tab_slot_w = (tabs_vis > 0) ? bar_inner_w / tabs_vis : bar_inner_w;

    /* < nav arrow */
    if (show_left)
    {
        int eff = style_begin(RMSTYLE_NAV_ARROW, ups);
        LbTextDrawResized(0, 4 * ups / 16, eff, "<");
    }
    /* > nav arrow */
    if (show_right)
    {
        int eff = style_begin(RMSTYLE_NAV_ARROW, ups);
        LbTextDrawResized(sw - arrow_w, 4 * ups / 16, eff, ">");
    }

    for (int t = s_state.tab_scroll; t < s_state.tab_scroll + tabs_vis && t < tc; t++)
    {
        int tx        = arrow_w + (t - s_state.tab_scroll) * tab_slot_w;
        int is_active = (t == s_state.current_tab);

        if (is_active)
            UIRenderer_SubmitSolidBoxAlpha(tx, 0, tab_slot_w, tab_bar_h, 15, 0.25f);

        RendMenuStyleId tab_style = is_active ? RMSTYLE_TAB_ACTIVE : RMSTYLE_TAB_INACTIVE;
        int eff_ups = style_begin(tab_style, ups);

        int pad     = 4 * ups / 16;
        int inner_w = tab_slot_w - 2 * pad;
        int label_w = LbTextStringWidthM(tabs[t].name, eff_ups);
        /* Clamp relative x so long names start at left edge of inner area */
        int rel_x   = (inner_w - label_w) / 2;
        if (rel_x < 0) rel_x = 0;
        LbTextSetWindow(tx + pad, 4 * ups / 16, inner_w, lh + 4 * ups / 16);
        LbTextDrawResized(rel_x, 0, eff_ups, tabs[t].name);
    }
    LbTextSetWindow(0, 0, sw, sh);

    /* --- Entry rows for current tab --- */
    if (s_state.current_tab < tc)
    {
        const RendMenuTab* tab = &tabs[s_state.current_tab];
        int end = s_state.scroll_top + vis;
        if (end > tab->count) end = tab->count;

        for (int i = s_state.scroll_top; i < end; i++)
        {
            const RendMenuEntry* e = &entries[tab->first_entry_idx + i];
            int ry           = rows_y + (i - s_state.scroll_top) * row_h;
            int is_focused   = (i == s_state.focused_in_tab);
            int is_available = (e->is_enabled == NULL || e->is_enabled());

            if (is_focused)
                UIRenderer_SubmitSolidBoxAlpha(mx, ry, pw, row_h, 15, 0.22f);

            /* Cursor glyph */
            if (is_focused)
            {
                int eff = style_begin(RMSTYLE_CURSOR, ups);
                LbTextDrawResized(mx + 2 * ups / 16, ry + ups / 16, eff, ">");
            }

            /* Row label */
            RendMenuStyleId lbl = is_focused    ? RMSTYLE_LABEL_FOCUSED
                                : !is_available ? RMSTYLE_LABEL_DISABLED
                                :                 RMSTYLE_LABEL;
            int eff_lbl = style_begin(lbl, ups);
            LbTextDrawResized(mx + 12 * ups / 16, ry + ups / 16, eff_lbl, e->label);

            /* Value string */
            char valbuf[48];
            entry_value_str(e, valbuf, sizeof(valbuf));
            if (valbuf[0] != '\0')
            {
                if (is_focused && is_available)
                {
                    /* Draw < value > with the arrow and value in distinct styles */
                    int eff_arr = style_begin(RMSTYLE_ARROW, ups);
                    int space_w = LbTextStringWidthM(" ", eff_arr);
                    int arr_w   = LbTextStringWidthM("<", eff_arr) + space_w;

                    int eff_val = style_begin(RMSTYLE_VALUE_FOCUSED, ups);
                    int val_w   = LbTextStringWidthM(valbuf, eff_val);

                    int total   = arr_w + val_w + arr_w;
                    int base_x  = mx + pw - total - 6 * ups / 16;
                    int text_y  = ry + ups / 16;

                    style_begin(RMSTYLE_ARROW, ups);
                    LbTextDrawResized(base_x, text_y, eff_arr, "<");

                    style_begin(RMSTYLE_VALUE_FOCUSED, ups);
                    LbTextDrawResized(base_x + arr_w, text_y, eff_val, valbuf);

                    style_begin(RMSTYLE_ARROW, ups);
                    LbTextDrawResized(base_x + arr_w + val_w + space_w,
                                      text_y, eff_arr, ">");
                }
                else
                {
                    int eff_val = style_begin(RMSTYLE_VALUE, ups);
                    int val_w   = LbTextStringWidthM(valbuf, eff_val);
                    LbTextDrawResized(mx + pw - val_w - 6 * ups / 16,
                                      ry + ups / 16, eff_val, valbuf);
                }
            }
        }
    }

    /* --- Description bar --- */
    int desc_y = my + ph - desc_h;
    UIRenderer_SubmitSolidBoxAlpha(mx, desc_y, pw, desc_h, 0, 0.6f);
    const RendMenuEntry* focused = get_focused_entry();
    if (focused && focused->desc)
    {
        int eff = style_begin(RMSTYLE_DESC, ups);
        LbTextDrawResized(mx + 4 * ups / 16, desc_y + 2 * ups / 16, eff,
                          focused->desc);
    }

    /* --- Restore all text rendering state --- */
    LbTextSetClipWindow(save_cx, save_cy, save_cx + save_cw, save_cy + save_ch);
    LbTextSetJustifyWindow(save_jx, save_jy, save_jw);
    LbTextSetFont(save_font);
    lbDisplay.DrawFlags = save_draw_flags;
}
