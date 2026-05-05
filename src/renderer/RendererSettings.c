/******************************************************************************/
// Dungeon Keeper - Renderer Configuration
/******************************************************************************/
/** @file RendererSettings.c
 *     Global renderer settings instance and reset function.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererSettings.h"
#include "thing_list.h"   // TCls_* enum values for outline class mask default

RendererSettings g_renderer_settings;

void RendererSettings_Reset(void)
{
    /* Startup-only */
    g_renderer_settings.palette_mode          = RENDERER_PALETTE_INDEXED;
    g_renderer_settings.zoom_box_mode         = RENDERER_ZBM_OVERHEAD;

    /* Shade / brightness */
    g_renderer_settings.shade_fullbright      = 0.0f;
    g_renderer_settings.shade_ambient         = 0.0f;
    g_renderer_settings.shade_scale           = 1.0f;
    g_renderer_settings.shade_gamma           = 1.0f;

    /* Texture filtering */
    g_renderer_settings.tile_filter           = RENDERER_FILTER_NEAREST;
    g_renderer_settings.ui_sprite_filter      = RENDERER_FILTER_NEAREST;
    g_renderer_settings.shadow_filter         = RENDERER_FILTER_NEAREST;

    /* Transparency */
    g_renderer_settings.transpar4_alpha       = 0.5f;
    g_renderer_settings.transpar8_alpha       = 0.25f;

    /* Glow */
    g_renderer_settings.glow_intensity        = 1.0f;
    g_renderer_settings.glow_blend_mode       = RENDERER_GLOW_ADDITIVE;

    /* Shadow */
    g_renderer_settings.shadow_darkness_scale = 1.0f;
    g_renderer_settings.shadow_depth_test     = 0;

    /* Remap/tint — CPU fallback until Phase 3 GPU shader is wired in */
    g_renderer_settings.tint_mode             = RENDERER_TINT_CPU;
    g_renderer_settings.tint_strength         = 1.0f;

    /* Lighting pipeline */
    g_renderer_settings.lighting_mode         = RENDERER_LIGHTING_SOFTWARE;

    /* Darkness mode */
    g_renderer_settings.darkness_mode         = RENDERER_DARKNESS_PALETTE;
    g_renderer_settings.fog_speed             = 1.0f;
    g_renderer_settings.fog_density           = 0.4f;

    /* Creature outline */
    g_renderer_settings.creature_outline_enable     = 1;
    g_renderer_settings.creature_outline_alpha      = 0.5f;
    g_renderer_settings.creature_outline_class_mask = (1u << TCls_Creature) | (1u << TCls_DeadCreature);

    /* Debug */
    g_renderer_settings.wireframe             = 0;
    g_renderer_settings.show_depth            = 0;
}

#include "post_inc.h"
