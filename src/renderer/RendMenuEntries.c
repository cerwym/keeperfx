/******************************************************************************/
// Dungeon Keeper - Renderer Settings Menu
/******************************************************************************/
/** @file RendMenuEntries.c
 *     Static entry table for the renderer settings overlay.
 *
 *     Each entry maps one RendererSettings field to a widget kind, display
 *     label, description, and (optionally) an availability predicate.
 *     The table is the single source of truth for what appears in the menu —
 *     adding a new renderer setting means adding one row here.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendMenu.h"
#include "renderer/RendererSettings.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

/* ---------------------------------------------------------------------------
 * Availability predicates
 * --------------------------------------------------------------------------
 * Keep these tiny so the compiler can inline them.  They read the live
 * g_renderer_settings struct, so they reflect the current state every frame.
 * ------------------------------------------------------------------------- */

/** Shade sliders are only meaningful when fullbright is off. */
static int en_not_fullbright(void)
{
    return g_renderer_settings.shade_fullbright < 0.5f;
}

/** Fog knobs are only meaningful in Fog darkness mode. */
static int en_fog_mode(void)
{
    return g_renderer_settings.darkness_mode == RENDERER_DARKNESS_FOG;
}

/** Settings that require the GPU render path (OpenGL / Vulkan). */
static int en_gpu(void)
{
    return RendererHasGPURenderPath();
}

/** Creature outline alpha is only editable when the outline is enabled. */
static int en_outline_enabled(void)
{
    return g_renderer_settings.creature_outline_mode != RENDERER_OUTLINE_NONE;
}

/** Shadow colour / transparency sliders are only meaningful when shadows are
 *  not disabled (shadow_type != RENDERER_SHADOW_OFF). */
static int en_shadow_active(void)
{
    return g_renderer_settings.shadow_type != RENDERER_SHADOW_OFF;
}

/* ---------------------------------------------------------------------------
 * Shared option-label arrays (shared across entries that use the same labels)
 * ------------------------------------------------------------------------- */

static const char* s_filter_opts[]      = { "Nearest", "Linear" };
static const char* s_darkness_opts[]    = { "Linear", "Palette", "Fog" };
static const char* s_lighting_opts[]    = { "Software", "Modern" };
static const char* s_glow_blend_opts[]  = { "Additive", "Screen" };
static const char* s_zoom_opts[]        = { "Overhead", "Isometric" };
static const char* s_shadow_type_opts[] = { "Off", "1", "2", "3", "4", "Circle" };
static const char* s_outline_mode_opts[]= { "Off", "Silhouette", "Edge" };
static const char* s_lens_color_opts[]  = { "Accurate", "Truecolor" };

/* ---------------------------------------------------------------------------
 * Entry table
 * --------------------------------------------------------------------------
 * C99 designated initialisers; union member is accessed via .w.<kind>.
 * SECTION rows use only .label — all other fields are zero/NULL by default.
 * The table ends with a sentinel entry where .label == NULL.
 * ------------------------------------------------------------------------- */

static RendMenuEntry s_entries[] = {

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "LIGHTING" },

    { RMENU_WIDGET_CYCLE, "Darkness mode",
      "How dark/unexplored areas are rendered",
      NULL,
      .w.cycle = { &g_renderer_settings.darkness_mode,
                   s_darkness_opts, 3 } },

    { RMENU_WIDGET_TOGGLE_F, "Fullbright",
      "Disable all shading — everything rendered at maximum brightness",
      NULL,
      .w.toggle_f = { &g_renderer_settings.shade_fullbright } },

    { RMENU_WIDGET_SLIDER_F, "Ambient light",
      "Minimum brightness received even in the deepest shadow  [0..1]",
      en_not_fullbright,
      .w.slider_f = { &g_renderer_settings.shade_ambient,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Brightness",
      "Overall brightness multiplier applied after DK shade lookup  [0.1..3.0]",
      en_not_fullbright,
      .w.slider_f = { &g_renderer_settings.shade_scale,
                      0.1f, 3.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Gamma",
      "Shade-curve gamma: >1 brightens mid-tones, <1 deepens shadows  [0.1..3.0]",
      en_not_fullbright,
      .w.slider_f = { &g_renderer_settings.shade_gamma,
                      0.1f, 3.0f, 0.05f } },

    { RMENU_WIDGET_CYCLE, "Lighting pipeline",
      "Software: DK-accurate Gouraud shading.  Modern: GPU dynamic lights (OpenGL)",
      en_gpu,
      .w.cycle = { &g_renderer_settings.lighting_mode,
                   s_lighting_opts, 2 } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "FOG" },

    { RMENU_WIDGET_SLIDER_F, "Fog density",
      "Opacity of the animated fog overlay (Fog darkness mode only)  [0..1]",
      en_fog_mode,
      .w.slider_f = { &g_renderer_settings.fog_density,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Fog speed",
      "Animation speed of the fog overlay (Fog darkness mode only)  [0.1..5.0]",
      en_fog_mode,
      .w.slider_f = { &g_renderer_settings.fog_speed,
                      0.1f, 5.0f, 0.1f } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "TEXTURES" },

    { RMENU_WIDGET_CYCLE, "World tile filter",
      "Texture filter for dungeon floor and wall tiles",
      NULL,
      .w.cycle = { &g_renderer_settings.tile_filter,
                   s_filter_opts, 2 } },

    { RMENU_WIDGET_CYCLE, "UI sprite filter",
      "Texture filter for panel sprites and interface graphics",
      NULL,
      .w.cycle = { &g_renderer_settings.ui_sprite_filter,
                   s_filter_opts, 2 } },

    { RMENU_WIDGET_CYCLE, "Shadow filter",
      "Texture filter for creature shadow textures",
      NULL,
      .w.cycle = { &g_renderer_settings.shadow_filter,
                   s_filter_opts, 2 } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "TRANSPARENCY" },

    { RMENU_WIDGET_SLIDER_F, "Quarter-alpha transparency",
      "Alpha for lightly-transparent sprites (e.g. water, magic effects)  [0..1]",
      NULL,
      .w.slider_f = { &g_renderer_settings.transpar4_alpha,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Eighth-alpha transparency",
      "Alpha for very-transparent sprites (e.g. ghost creatures)  [0..1]",
      NULL,
      .w.slider_f = { &g_renderer_settings.transpar8_alpha,
                      0.0f, 1.0f, 0.05f } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "GLOW" },

    { RMENU_WIDGET_SLIDER_F, "Glow intensity",
      "Scale multiplier for additive glow passes on magic and fire  [0..2]",
      NULL,
      .w.slider_f = { &g_renderer_settings.glow_intensity,
                      0.0f, 2.0f, 0.05f } },

    { RMENU_WIDGET_CYCLE, "Glow blend mode",
      "Additive: classic DK energy look.  Screen: softer, no over-exposure",
      NULL,
      .w.cycle = { &g_renderer_settings.glow_blend_mode,
                   s_glow_blend_opts, 2 } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "SHADOWS" },

    { RMENU_WIDGET_SLIDER_F, "Shadow darkness",
      "Multiplier on creature and object shadow opacity  [0..2]",
      en_shadow_active,
      .w.slider_f = { &g_renderer_settings.shadow_darkness_scale,
                      0.0f, 2.0f, 0.05f } },

    { RMENU_WIDGET_TOGGLE, "Shadow depth test",
      "Clip shadows against geometry so they don't bleed through walls",
      NULL,
      .w.toggle = { &g_renderer_settings.shadow_depth_test } },

    /* -- Creature Shadows divider -- */
    { RMENU_WIDGET_LABEL, "── Creature Shadows ──" },

    { RMENU_WIDGET_CYCLE, "Shadow type",
      "Off: disabled.  1-4: sprite shadow from N light sources.  Circle: soft circle blob",
      en_gpu,
      .w.cycle = { &g_renderer_settings.shadow_type,
                   s_shadow_type_opts, 6 } },

    { RMENU_WIDGET_SLIDER_I, "Max shadows / frame",
      "Global cap on creature shadows drawn per frame; 0 = unlimited  [0..64]",
      en_shadow_active,
      .w.slider_i = { &g_renderer_settings.shadow_max_count,
                      0, 64 } },

    { RMENU_WIDGET_SLIDER_F, "Shadow colour R",
      "Red tint of creature shadows; 0 = classic black darkening  [0..1]",
      en_shadow_active,
      .w.slider_f = { &g_renderer_settings.shadow_colour_r,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Shadow colour G",
      "Green tint of creature shadows  [0..1]",
      en_shadow_active,
      .w.slider_f = { &g_renderer_settings.shadow_colour_g,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Shadow colour B",
      "Blue tint of creature shadows  [0..1]",
      en_shadow_active,
      .w.slider_f = { &g_renderer_settings.shadow_colour_b,
                      0.0f, 1.0f, 0.05f } },

    { RMENU_WIDGET_SLIDER_F, "Shadow transparency",
      "Overall shadow intensity multiplier; 1 = full, 0 = invisible  [0..1]",
      en_shadow_active,
      .w.slider_f = { &g_renderer_settings.shadow_colour_a,
                      0.0f, 1.0f, 0.05f } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "CREATURES" },

    { RMENU_WIDGET_CYCLE, "Creature outlines",
      "Outline mode for occluded creatures: Off / Silhouette / Edge",
      NULL,
      .w.cycle = { &g_renderer_settings.creature_outline_mode,
                   s_outline_mode_opts, 3 } },

    { RMENU_WIDGET_SLIDER_F, "Outline alpha",
      "Opacity of the depth-fail creature outline  [0..1]",
      en_outline_enabled,
      .w.slider_f = { &g_renderer_settings.creature_outline_alpha,
                      0.0f, 1.0f, 0.05f } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "LENS" },

    { RMENU_WIDGET_CYCLE, "Lens colour mode",
      "Possession lens colour fidelity.  Accurate: 8-bit paletted, bit-exact "
      "with software.  Truecolor: smooth RGBA (for truecolor lens textures)",
      en_gpu,
      .w.cycle = { &g_renderer_settings.lens_color_mode,
                   s_lens_color_opts, 2 } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "VIEW" },

    { RMENU_WIDGET_CYCLE, "Mini-map mode",
      "Overhead: classic flat map.  Isometric: live 3D picture-in-picture (OpenGL only)",
      en_gpu,
      .w.cycle = { &g_renderer_settings.zoom_box_mode,
                   s_zoom_opts, 2 } },

    { RMENU_WIDGET_SLIDER_F, "Tint strength",
      "Intensity of palette remap tint effects (e.g. evil eye, possession)  [0..1]",
      NULL,
      .w.slider_f = { &g_renderer_settings.tint_strength,
                      0.0f, 1.0f, 0.05f } },

    /* ------------------------------------------------------------------ */
    { RMENU_WIDGET_SECTION, "DEBUG" },

    { RMENU_WIDGET_TOGGLE, "Wireframe",
      "Render all geometry as wireframe outlines",
      NULL,
      .w.toggle = { &g_renderer_settings.wireframe } },

    { RMENU_WIDGET_TOGGLE, "Show depth buffer",
      "Visualise the depth buffer as a greyscale overlay",
      NULL,
      .w.toggle = { &g_renderer_settings.show_depth } },

    { RMENU_WIDGET_TOGGLE, "GUI hit-box overlay",
      "Draw coloured outlines over all active GuiButton collision rectangles",
      NULL,
      .w.toggle = { &g_renderer_settings.debug_gui_hitboxes } },

    /* ---- sentinel ---- */
    { RMENU_WIDGET_SECTION, NULL },
};

/* ---------------------------------------------------------------------------
 * Accessor
 * ------------------------------------------------------------------------- */

const RendMenuEntry* RendMenu_GetEntries(int* out_count)
{
    static int count = -1;
    if (count < 0)
    {
        count = 0;
        const RendMenuEntry* e = s_entries;
        while (e->label != NULL) { ++e; ++count; }
    }
    if (out_count)
        *out_count = count;
    return s_entries;
}

/* ---------------------------------------------------------------------------
 * Tab accessor — built once from the section rows in the entry table
 * ------------------------------------------------------------------------- */

#define RMENU_MAX_TABS 16

static RendMenuTab s_tabs[RMENU_MAX_TABS];
static int         s_tab_count = -1;

const RendMenuTab* RendMenu_GetTabs(int* out_count)
{
    if (s_tab_count < 0)
    {
        int ecount = 0;
        const RendMenuEntry* entries = RendMenu_GetEntries(&ecount);
        s_tab_count = 0;

        for (int i = 0; i < ecount && s_tab_count < RMENU_MAX_TABS; i++)
        {
            if (entries[i].kind != RMENU_WIDGET_SECTION)
                continue;

            s_tabs[s_tab_count].name            = entries[i].label;
            s_tabs[s_tab_count].first_entry_idx = i + 1;

            /* Count non-section entries until the next SECTION or end */
            int cnt = 0;
            for (int j = i + 1; j < ecount &&
                 entries[j].kind != RMENU_WIDGET_SECTION; j++)
                cnt++;
            s_tabs[s_tab_count].count = cnt;
            s_tab_count++;
        }
    }
    if (out_count)
        *out_count = s_tab_count;
    return s_tabs;
}
