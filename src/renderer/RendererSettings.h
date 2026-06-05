/******************************************************************************/
// Dungeon Keeper - Renderer Configuration
/******************************************************************************/
/** @file RendererSettings.h
 *     Run-time configurable renderer knobs.
 * @par Design:
 *     A C-compatible POD struct so that C translation units (config parsing,
 *     console commands) can read and modify settings without pulling in C++
 *     headers.  The active settings are stored in the global g_renderer_settings
 *     and applied to the active renderer via RendererApplySettings().
 *
 *     Default values (set by RendererSettings_Reset) preserve the original
 *     software-renderer behaviour exactly — no visual change on first run.
 *
 * @par Persistence:
 *     Startup-only knobs (e.g. palette_mode) come from keeperfx.cfg text config.
 *     Per-session knobs are read/written from renderer_settings.dat.
 *     These DO NOT live in GameSettings (which is a #pragma pack(1) binary blob).
 */
/******************************************************************************/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Palette mode constants (palette_mode field)
 * ------------------------------------------------------------------------- */
/** 256-colour DK indexed atlas: all sprite textures stored as GL_R8 palette
 *  indices; a palette LUT is uploaded once per palette change.  Exact colour
 *  match with software renderer.  Startup-only — requires atlas re-upload. */
#define RENDERER_PALETTE_INDEXED    0

/** True-colour RGBA atlas: sprites pre-decoded to GL_RGBA8 at load time.
 *  Enables bilinear filtering and future HDR/bloom passes at the cost of
 *  ~4× VRAM per sprite sheet.  Startup-only. */
#define RENDERER_PALETTE_TRUECOLOUR 1

/* ---------------------------------------------------------------------------
 * Texture filter constants (tile_filter, ui_sprite_filter, shadow_filter)
 * ------------------------------------------------------------------------- */
#define RENDERER_FILTER_NEAREST 0   /**< GL_NEAREST — pixel-art style (default) */
#define RENDERER_FILTER_LINEAR  1   /**< GL_LINEAR  — bilinear smoothing        */

/* ---------------------------------------------------------------------------
 * Remap/tint mode constants (tint_mode field)
 * ------------------------------------------------------------------------- */
/** Palette remap performed on GPU via remap shader (future Phase 3). */
#define RENDERER_TINT_GPU   0
/** Palette remap performed on CPU via base-class SubmitSpriteRemap fallback. */
#define RENDERER_TINT_CPU   2

/* ---------------------------------------------------------------------------
 * Blend mode constants (glow_blend_mode field)
 * ------------------------------------------------------------------------- */
#define RENDERER_GLOW_ADDITIVE 0  /**< Additive blending (classic DK look, default) */
#define RENDERER_GLOW_SCREEN   1  /**< Screen blend (softer, no over-exposure)       */

/* ---------------------------------------------------------------------------
 * Zoom-box render mode constants (zoom_box_mode field)
 * These mirror ZoomBoxMode in RendererManager.h; duplicated here so that the
 * C-only config / console layers do not need to pull in the C++ renderer header.
 * ------------------------------------------------------------------------- */
/** Default: flat palette-indexed overhead map tiles. */
#define RENDERER_ZBM_OVERHEAD   0
/** Picture-in-picture isometric 3D view via FBO (OpenGL only). */
#define RENDERER_ZBM_ISOMETRIC  1

/* ---------------------------------------------------------------------------
 * Darkness mode constants (darkness_mode field)
 * ------------------------------------------------------------------------- */
/** Linear RGB multiply — clean fade toward black.  Default. */
#define RENDERER_DARKNESS_LINEAR     0
/** Palette fade-table LUT — samples the original DK fade table on the GPU.
 *  Reproduces the non-linear hue shifts of the software renderer. */
#define RENDERER_DARKNESS_PALETTE    1
/** Animated fog — dark areas receive a scrolling noise-based fog overlay
 *  instead of a flat colour multiply.  Purely cosmetic enhancement. */
#define RENDERER_DARKNESS_FOG        2

/* ---------------------------------------------------------------------------
 * Lighting pipeline mode constants (lighting_mode field)
 * ------------------------------------------------------------------------- */
/** Software-accurate lighting: per-vertex Gouraud shade from the DK fade table.
 *  Emulates the software renderer exactly.  Default. */
#define RENDERER_LIGHTING_SOFTWARE 0
/** Modern GPU lighting: per-fragment lightmap sampling + dynamic point lights.
 *  Requires GLModernWorldViewRenderer (Phase 3+).  Stub passthrough in Phase 2. */
#define RENDERER_LIGHTING_MODERN   1

/* ---------------------------------------------------------------------------
 * Main settings struct
 * ------------------------------------------------------------------------- */
typedef struct RendererSettings {
    /** Atlas colour mode.  RENDERER_PALETTE_INDEXED (0) or
     *  RENDERER_PALETTE_TRUECOLOUR (1).  Default: INDEXED.
     *
     *  Changing this via RendererApplySettings() triggers an immediate atlas
     *  rebuild so sprites are re-uploaded in the new format.  No restart needed.
     *  Switching to TRUECOLOUR uses ~4× more VRAM per sprite sheet. */
    int   palette_mode;

    /* --- Shade / brightness knobs (applied as uniforms each frame) --- */

    /** Fullbright toggle.  0 = normal shading; 1 = bypass v_shade (all
     *  geometry rendered at maximum brightness).  Default: 0. */
    float shade_fullbright;

    /** Ambient darkness floor.  Fraction of full brightness that even the
     *  darkest geometry receives.  0.0 = pure black in shadow (original),
     *  1.0 = no shading at all.  Range [0.0, 1.0].  Default: 0.0. */
    float shade_ambient;

    /** Overall brightness multiplier applied after DK's v_shade lookup.
     *  1.0 = original brightness.  Range > 0.  Default: 1.0. */
    float shade_scale;

    /** Shade curve gamma.  Values > 1.0 brighten mid-tones; < 1.0 deepen
     *  shadows.  Applied as pow(shade, 1/shade_gamma) to the shade fraction.
     *  1.0 = linear (original).  Default: 1.0. */
    float shade_gamma;

    /* --- Texture filtering --- */

    /** World tile texture filter.  NEAREST (0) or LINEAR (1).  Default: NEAREST. */
    int   tile_filter;

    /** UI/panel sprite texture filter.  Default: NEAREST. */
    int   ui_sprite_filter;

    /** Shadow texture filter.  Default: NEAREST. */
    int   shadow_filter;

    /* --- Transparency --- */

    /** Alpha value for Lb_SPRITE_TRANSPAR4 sprites (one-quarter alpha).
     *  Default: 0.5 (matches original visual weight). */
    float transpar4_alpha;

    /** Alpha value for Lb_SPRITE_TRANSPAR8 sprites (one-eighth alpha).
     *  Default: 0.25. */
    float transpar8_alpha;

    /* --- Glow / additive passes --- */

    /** Scale multiplier for k_glow_step contribution.  1.0 = original,
     *  0.0 = no glow.  Default: 1.0. */
    float glow_intensity;

    /** Glow blend equation.  ADDITIVE (0) or SCREEN (1).  Default: ADDITIVE. */
    int   glow_blend_mode;

    /* --- Shadow pass --- */

    /** Multiplier on the u_darkness uniform.  1.0 = original shadow depth.
     *  Default: 1.0. */
    float shadow_darkness_scale;

    /** Shadow depth test.  0 = disabled (original); 1 = wall-clipped (future).
     *  Default: 0. */
    int   shadow_depth_test;

    /** Zoom-box render mode.
     *  RENDERER_ZBM_OVERHEAD (0) = flat palette-indexed tiles (default, all backends).
     *  RENDERER_ZBM_ISOMETRIC (1) = picture-in-picture isometric 3D view via FBO
     *  (OpenGL only; other backends silently fall back to OVERHEAD at draw time). */
    int   zoom_box_mode;

    /* --- Palette remap / tint mode --- */

    /** Remap mode.  RENDERER_TINT_CPU (2) = CPU fallback via IBackend base
     *  (current default); RENDERER_TINT_GPU (0) = GPU shader (Phase 3+).
     *  Default: RENDERER_TINT_CPU. */
    int   tint_mode;

    /** Tint strength.  1.0 = full remap applied; 0.0 = original colours.
     *  Default: 1.0. */
    float tint_strength;

    /* --- Debug / developer knobs --- */

    /** Wireframe mode.  0 = off; 1 = GL_LINE.  Default: 0. */
    int   wireframe;

    /** Depth buffer visualisation.  0 = off; 1 = z-buffer shown as greyscale.
     *  Default: 0. */
    int   show_depth;

    /** GUI hit-box debug overlay.
     *  0 = off (default); 1 = draw coloured outline rectangles over every
     *  active GuiButton's collision box (pos_x/pos_y/width/height) and the
     *  minimap circular hit area.
     *  Green = normal button, yellow = currently hovered, cyan = minimap circle. */
    int   debug_gui_hitboxes;

    /* --- Lighting pipeline --- */

    /** Lighting pipeline mode.
     *  RENDERER_LIGHTING_SOFTWARE (0) = software-accurate: per-vertex Gouraud shade
     *  from DK fade table rows (default, visually identical to software renderer).
     *  RENDERER_LIGHTING_MODERN (1) = GPU dynamic lighting: per-fragment lightmap
     *  sample + point light accumulation (requires modern renderer, Phase 3+). */
    int   lighting_mode;

    /* --- Darkness mode --- */

    /** How dark areas are shaded.
     *  RENDERER_DARKNESS_LINEAR (0)  = linear RGB multiply (default).
     *  RENDERER_DARKNESS_PALETTE (1) = palette fade-table LUT (software-accurate).
     *  RENDERER_DARKNESS_FOG (2)     = animated fog effect on dark areas. */
    int   darkness_mode;

    /** Fog animation speed (darkness_mode == FOG only).  1.0 = default speed. */
    float fog_speed;

    /** Fog density / opacity (darkness_mode == FOG only).  Range [0,1].
     *  0.0 = transparent; 1.0 = fully opaque fog.  Default: 0.4. */
    float fog_density;

    /* --- Creature depth-fail outline --- */

    /** Draw an owner-coloured silhouette where a creature sprite is occluded
     *  by geometry (walls, columns).  Uses a depth-fail pass so the outline
     *  is visible only through solid objects.
     *  0 = disabled; 1 = enabled.  Default: 1. */
    int   creature_outline_enable;

    /** Alpha of the depth-fail creature outline.  Range [0.0, 1.0].
     *  Default: 0.5. */
    float creature_outline_alpha;

    /** Bitmask of ThingClass (TCls_*) values that receive the depth-fail outline.
     *  Bit N is set iff things with class_id == N should be outlined.
     *  Evaluated by the engine (engine_render.c) before passing wants_outline
     *  to the renderer; the renderer itself never inspects ThingClass.
     *  Default: (1<<TCls_Creature)|(1<<TCls_DeadCreature). */
    unsigned int creature_outline_class_mask;

} RendererSettings;

/** Global active renderer settings.  Modified by config parsing and console
 *  commands; applied to the renderer via RendererApplySettings(). */
extern RendererSettings g_renderer_settings;

/** Reset g_renderer_settings to all defaults.
 *  Defaults preserve exact original software-renderer visual behaviour. */
void RendererSettings_Reset(void);

/** Clamp all fields in g_renderer_settings to their valid ranges.
 *  Call after loading from file or receiving console input to prevent
 *  out-of-range enum values from causing array-index or UB issues.
 *  Also rounds float-backed boolean fields (shade_fullbright) to 0.0f/1.0f. */
void RendererSettings_Sanitize(void);

/** Load renderer preferences from the platform user-pref directory.
 *  File: <GetUserPrefDir()>/renderer_prefs.ini
 *  Missing file is silently ignored (first run).  Only fields present in the
 *  file are updated; absent fields retain whatever value Reset() or keeperfx.cfg
 *  left them at.  Calls RendererApplySettings() on success. */
void RendererSettings_Load(void);

/** Save the current g_renderer_settings to the platform user-pref directory.
 *  File: <GetUserPrefDir()>/renderer_prefs.ini
 *  Call this whenever the player confirms a change via the settings menu, and
 *  on clean game exit. */
void RendererSettings_Save(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
