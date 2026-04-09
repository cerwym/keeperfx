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
#ifndef RENDERER_SETTINGS_H
#define RENDERER_SETTINGS_H

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

    /* --- Lighting pipeline --- */

    /** Lighting pipeline mode.
     *  RENDERER_LIGHTING_SOFTWARE (0) = software-accurate: per-vertex Gouraud shade
     *  from DK fade table rows (default, visually identical to software renderer).
     *  RENDERER_LIGHTING_MODERN (1) = GPU dynamic lighting: per-fragment lightmap
     *  sample + point light accumulation (requires modern renderer, Phase 3+). */
    int   lighting_mode;

} RendererSettings;

/** Global active renderer settings.  Modified by config parsing and console
 *  commands; applied to the renderer via RendererApplySettings(). */
extern RendererSettings g_renderer_settings;

/** Reset g_renderer_settings to all defaults.
 *  Defaults preserve exact original software-renderer visual behaviour. */
void RendererSettings_Reset(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RENDERER_SETTINGS_H */
