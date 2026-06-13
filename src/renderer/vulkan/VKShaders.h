/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VKShaders.h
 *     Embedded GLSL 450 shader sources and shaderc runtime compilation helpers
 *     for the Vulkan renderer.
 *
 * Descriptor set / push-constant conventions:
 *   Push constants  — small per-draw scalars and vectors (≤128 bytes, all shaders)
 *   set=0, binding=N — textures / samplers (N per-shader, documented per shader)
 *
 * Push constant block layout (all shaders share this struct, use only what they need):
 *   offset  0: vec2  screen_size   — render target size in pixels
 *   offset  8: float z_ndc         — NDC depth [-1, 1]
 *   offset 12: float alpha         — sprite alpha (1.0/0.5/0.25)
 *   offset 16: vec4  clip_rect     — (x0, y0, x1, y1) screen pixels
 *   offset 32: float clip_radius   — corner radius (<0 = no clip)
 *   offset 36: float clip_screen_h — screen height for Y flip
 *   offset 40: float remap_row     — fade-table row [0..255]
 *   offset 44: float tint_factor   — possession/pain red tint [0, 1]
 *   offset 48: vec2  center_map    — zoom: centre in map texels
 *   offset 56: vec2  screen_center — zoom: centre in screen pixels
 *   offset 64: float zoom_scale    — zoom: source texels per pixel
 *   offset 68: vec2  inv_map_size  — zoom: (1/W, 1/H)
 *   offset 76: float map_step      — map-fade: step [0..32]
 *   offset 80: float fullbright    — world: 0=normal, 1=bypass shade
 *   offset 84: float ambient       — world: darkness floor [0,1]
 *   offset 88: float shade_scale   — world: brightness multiplier
 *   offset 92: float shade_gamma   — world: shade curve exponent
 *   offset 96: int   lighting_mode — world: 0=software, 1=modern
 *   offset 100: int  darkness_mode — world: 0=linear, 1=palette LUT, 2=fog
 *   offset 104: int  tile_filter   — world: 0=nearest, 1=bilinear
 *   offset 108: float missing_tile — world: 1.0 = show checkerboard
 *   offset 112: float time         — world: seconds (fog animation)
 *   offset 116: float fog_speed    — world: fog scroll speed
 *   offset 120: float fog_density  — world: fog opacity
 *   offset 124: float ndc_z_shadow — shadow: NDC depth
 */
/******************************************************************************/
#pragma once
#ifndef VKSHADERS_H
#define VKSHADERS_H

#ifdef RENDERER_VULKAN_ENABLED

#ifdef RENDERER_VK_SHADERC_AVAILABLE
#  include <shaderc/shaderc.h>
#endif

#include <cstdint>
#include <vector>

/******************************************************************************/
// Compilation helper
/******************************************************************************/

#ifdef RENDERER_VK_SHADERC_AVAILABLE
/**
 * Compile a GLSL source string to SPIR-V using shaderc.
 * @param compiler    shaderc_compiler_t (caller owns, must be valid)
 * @param src         GLSL source text
 * @param stage       shaderc_shader_kind (shaderc_glsl_vertex_shader etc.)
 * @param debug_name  Human-readable name for error messages
 * @return SPIR-V word stream, or empty vector on failure.
 */
std::vector<uint32_t> VKShaders_Compile(shaderc_compiler_t compiler,
                                         const char* src,
                                         shaderc_shader_kind stage,
                                         const char* debug_name);
#endif

/******************************************************************************/
// GLSL 450 shader sources
// All shaders use the unified push-constant block defined at the top of this file.
// Only the fields relevant to that shader are accessed.
/******************************************************************************/

// ── Shared push-constant block preamble ─────────────────────────────────────
//
// Embedded in each shader source that references push constants.
// The struct is identical across all shaders so pipeline layouts are compatible.
//
static constexpr const char* VK_PC_BLOCK = R"glsl(
layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
} pc;
)glsl";

/******************************************************************************/
// UI shaders
/******************************************************************************/

// Shared UI vertex shader.  Attribute layout matches GLUIVertex.
static constexpr const char* VK_UI_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in float a_z;
layout(location = 4) in float a_mode;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    vec2 ndc;
    ndc.x = (a_pos.x / pc.screen_size.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (a_pos.y / pc.screen_size.y) * 2.0;
    gl_Position = vec4(ndc, a_z, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
}
)glsl";

// Palette-indexed atlas sprite.
// set=0 binding=0 : R8 sprite atlas
// set=0 binding=1 : RGBA8 256×1 palette
static constexpr const char* VK_UI_SPRITE_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite_atlas;
layout(set=0, binding=1) uniform sampler2D u_palette;

void main()
{
    float idx = texture(u_sprite_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 pal = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";

// Solid colour (no texture).
static constexpr const char* VK_UI_SOLID_FRAG = R"glsl(
#version 450
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = v_color; }
)glsl";

// Palette-indexed atlas sprite with a player-colour remap table.
// set=0 binding=0 : R8 sprite atlas
// set=0 binding=1 : RGBA8 256×1 palette
// set=0 binding=2 : R8 256×256 fade/remap LUT
static constexpr const char* VK_UI_REMAP_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite_atlas;
layout(set=0, binding=1) uniform sampler2D u_palette;
layout(set=0, binding=2) uniform sampler2D u_fade_table;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
} pc;

void main()
{
    float idx_f = texture(u_sprite_atlas, v_uv).r;
    if (idx_f < (0.5 / 255.0)) discard;
    float remap_y    = (pc.remap_row + 0.5) / 256.0;
    float remapped_f = texture(u_fade_table, vec2(idx_f, remap_y)).r;
    vec4 pal = texture(u_palette, vec2(remapped_f, 0.5));
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";

// RGBA8 font atlas — alpha channel is the glyph mask.
// set=0 binding=0 : RGBA8 font atlas
static constexpr const char* VK_UI_FONT_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_font_atlas;

void main()
{
    float alpha = texture(u_font_atlas, v_uv).a;
    if (alpha < 0.1) discard;
    fragColor = vec4(v_color.rgb, alpha * v_color.a);
}
)glsl";

// Sprite atlas as discard mask; outputs flat vertex colour.
// set=0 binding=0 : R8 sprite atlas
static constexpr const char* VK_UI_COLORED_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  vec4 v_color;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite_atlas;

void main()
{
    float idx = texture(u_sprite_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = v_color;
}
)glsl";

// FBO / PiP composite — RGBA8 FBO texture with optional rounded-corner clip.
// set=0 binding=0 : RGBA8 FBO colour texture
static constexpr const char* VK_UI_FBO_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_fbo_tex;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
} pc;

void main()
{
    if (pc.clip_radius >= 0.0)
    {
        vec2 p       = vec2(gl_FragCoord.x, pc.clip_screen_h - gl_FragCoord.y);
        vec2 center  = (pc.clip_rect.xy + pc.clip_rect.zw) * 0.5;
        vec2 halfSz  = (pc.clip_rect.zw - pc.clip_rect.xy) * 0.5;
        vec2 d       = abs(p - center) - halfSz + pc.clip_radius;
        float dist   = length(max(d, 0.0)) - pc.clip_radius;
        if (dist > 0.0) discard;
    }
    fragColor = texture(u_fbo_tex, v_uv);
}
)glsl";

/******************************************************************************/
// Blit shaders — palette decode from R8 staging buffer
/******************************************************************************/

// Fullscreen quad vertex — passthrough UV.
static constexpr const char* VK_BLIT_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)glsl";

// Palette decode: R8 index texture + 256×1 RGBA8 palette → swapchain.
// set=0 binding=0 : R8  index texture (CPU staging buffer contents)
// set=0 binding=1 : RGBA8 256×1 palette
static constexpr const char* VK_BLIT_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_index;
layout(set=0, binding=1) uniform sampler2D u_palette;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
} pc;

void main()
{
    float idx        = texture(u_index, v_uv).r;
    float is_nonzero = step(0.5 / 256.0, idx);
    vec4  pal_color  = texture(u_palette, vec2(idx, 0.5));
    pal_color.a = 1.0;
    fragColor = mix(vec4(1.0, 0.0, 0.0, pc.tint_factor), pal_color, is_nonzero);
}
)glsl";

// Raw-image blit (no index-0 transparency, always opaque).
static constexpr const char* VK_RAWBLIT_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_index;
layout(set=0, binding=1) uniform sampler2D u_palette;

void main()
{
    float idx = texture(u_index, v_uv).r;
    vec4  pal = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(pal.rgb, 1.0);
}
)glsl";

/******************************************************************************/
// World rendering shaders
/******************************************************************************/

// Tile geometry — WorldVertex layout (7 attributes, location 0-6).
static constexpr const char* VK_WORLD_VERT = R"glsl(
#version 450
layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in float a_shade;
layout(location = 3) in vec2  a_stl;
layout(location = 4) in float a_camera_z;
layout(location = 5) in float a_layer;
layout(location = 6) in vec3  aWorldPos;

layout(location = 0) out vec2  v_uv;
layout(location = 1) out float v_shade;
layout(location = 2) out vec2  v_stl;
layout(location = 3) out float v_layer;
layout(location = 4) out vec3  vWorldPos;

void main()
{
    float w = max(a_camera_z, 1.0);
    gl_Position = vec4(a_pos.xy * w, a_pos.z * w, w);
    v_uv      = a_uv;
    v_shade   = a_shade;
    v_stl     = a_stl;
    v_layer   = a_layer;
    vWorldPos = aWorldPos;
}
)glsl";

// Tile fragment — palette-indexed tile atlas + shading modes.
// set=0 binding=0 : R8   tile atlas array (sampler2DArray)
// set=0 binding=1 : RGBA8 256×1 palette
// set=0 binding=2 : R16UI subtile lightmap (usampler2D)
// set=0 binding=3 : R8   256×256 fade/remap LUT
static constexpr const char* VK_WORLD_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2  v_uv;
layout(location = 1) in  float v_shade;
layout(location = 2) in  vec2  v_stl;
layout(location = 3) in  float v_layer;
layout(location = 4) in  vec3  vWorldPos;
layout(location = 0) out vec4  fragColor;

layout(set=0, binding=0) uniform sampler2DArray u_tile_atlas;
layout(set=0, binding=1) uniform sampler2D      u_palette;
layout(set=0, binding=2) uniform usampler2D     u_lightmap;
layout(set=0, binding=3) uniform sampler2D      u_fade_table;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
} pc;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise2d(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++) { v += a * noise2d(p); p *= 2.0; a *= 0.5; }
    return v;
}

void main()
{
    if (pc.missing_tile > 0.5)
    {
        ivec2 cell = ivec2(gl_FragCoord.xy / 8.0);
        fragColor  = ((cell.x + cell.y) & 1) == 0
                     ? vec4(1.0, 0.0, 1.0, 1.0)
                     : vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float raw_shade;
    if (pc.lighting_mode == 0) {
        raw_shade = mix(v_shade, 1.0, pc.fullbright);
    } else {
        vec2  lm_uv  = v_stl / vec2(511.0, 511.0);
        uint  lm_raw = texture(u_lightmap, lm_uv).r;
        raw_shade = float(lm_raw) / 8192.0;
        raw_shade = mix(raw_shade, 1.0, pc.fullbright);
    }

    float fade_v = 0.0;
    if (pc.darkness_mode != 0) {
        float fade_row = raw_shade * 32.0;
        fade_v = (clamp(fade_row, 0.0, 63.0) + 0.5) / 256.0;
    }

    float pal_idx;
    vec4  col;

    if (pc.tile_filter == 1) {
        vec2 tex_size = vec2(textureSize(u_tile_atlas, 0).xy);
        vec2 px   = v_uv * tex_size - 0.5;
        vec2 f    = fract(px);
        vec2 base = (floor(px) + 0.5) / tex_size;
        vec2 st   = 1.0 / tex_size;
        float layer = v_layer;
        float idx00 = texture(u_tile_atlas, vec3(base, layer)).r;
        float idx10 = texture(u_tile_atlas, vec3(base + vec2(st.x, 0.0), layer)).r;
        float idx01 = texture(u_tile_atlas, vec3(base + vec2(0.0, st.y), layer)).r;
        float idx11 = texture(u_tile_atlas, vec3(base + st, layer)).r;
        if (pc.darkness_mode != 0) {
            float r00 = texture(u_fade_table, vec2(idx00, fade_v)).r;
            float r10 = texture(u_fade_table, vec2(idx10, fade_v)).r;
            float r01 = texture(u_fade_table, vec2(idx01, fade_v)).r;
            float r11 = texture(u_fade_table, vec2(idx11, fade_v)).r;
            vec4 c00 = texture(u_palette, vec2(r00, 0.5));
            vec4 c10 = texture(u_palette, vec2(r10, 0.5));
            vec4 c01 = texture(u_palette, vec2(r01, 0.5));
            vec4 c11 = texture(u_palette, vec2(r11, 0.5));
            col = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
        } else {
            vec4 c00 = texture(u_palette, vec2(idx00, 0.5));
            vec4 c10 = texture(u_palette, vec2(idx10, 0.5));
            vec4 c01 = texture(u_palette, vec2(idx01, 0.5));
            vec4 c11 = texture(u_palette, vec2(idx11, 0.5));
            col = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
        }
        pal_idx = idx00;
    } else {
        pal_idx = texture(u_tile_atlas, vec3(v_uv, v_layer)).r;
        col = texture(u_palette, vec2(pal_idx, 0.5));
    }

    float remapped = pal_idx;
    vec3 pal_color = col.rgb;
    if (pc.darkness_mode != 0 && pc.tile_filter != 1) {
        remapped   = texture(u_fade_table, vec2(pal_idx, fade_v)).r;
        pal_color  = texture(u_palette, vec2(remapped, 0.5)).rgb;
    } else if (pc.darkness_mode != 0) {
        pal_color = col.rgb;
    }

    if (pc.darkness_mode == 1) {
        fragColor = vec4(pal_color, 1.0);
    } else if (pc.darkness_mode == 2) {
        float fog_mask = 1.0 - clamp(raw_shade, 0.0, 1.0);
        if (fog_mask > 0.01) {
            vec2  fog_uv = gl_FragCoord.xy * 0.008
                         + vec2(pc.time * pc.fog_speed * 0.4,
                                pc.time * pc.fog_speed * 0.25);
            float n = fbm(fog_uv);
            vec3  fog_color  = vec3(0.06, 0.10, 0.06);
            float fog_alpha  = smoothstep(0.25, 0.65, n) * fog_mask * pc.fog_density;
            fragColor = vec4(mix(pal_color, fog_color, fog_alpha), 1.0);
        } else {
            fragColor = vec4(pal_color, 1.0);
        }
    } else {
        float shade = max(raw_shade, pc.ambient);
        shade = pow(clamp(shade * pc.shade_scale, 0.0, 1.0), pc.shade_gamma);
        fragColor = vec4(col.rgb * shade, 1.0);
    }
}
)glsl";

/******************************************************************************/
// Keeper sprite shaders
/******************************************************************************/

// Vertex shader: screen-space position + u_z_ndc depth.
static constexpr const char* VK_KSPR_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
} pc;

void main()
{
    vec2 ndc;
    ndc.x = a_pos.x / pc.screen_size.x * 2.0 - 1.0;
    ndc.y = 1.0 - a_pos.y / pc.screen_size.y * 2.0;
    gl_Position = vec4(ndc, pc.z_ndc, 1.0);
    v_uv = a_uv;
}
)glsl";

// Fragment: palette-indexed R8 sprite (single atlas page).
// set=0 binding=0 : R8   sprite atlas
// set=0 binding=1 : RGBA8 256×1 palette
static constexpr const char* VK_KSPR_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite;
layout(set=0, binding=1) uniform sampler2D u_palette;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
} pc;

void main()
{
    float idx = texture(u_sprite, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 color = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(color.rgb, pc.alpha);
}
)glsl";

// Fragment: sprite array atlas with CLUT.
// set=0 binding=0 : R8   sprite atlas array (sampler2DArray)
// set=0 binding=1 : RGBA8 NxN CLUT (rows=remaps, cols=palette indices)
static constexpr const char* VK_KSPR_ARRAY_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2DArray u_sprite;
layout(set=0, binding=1) uniform sampler2D      u_clut;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
    float layer;
    float clut_v;
} pc;

void main()
{
    float idx = texture(u_sprite, vec3(v_uv, pc.layer)).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 color = texture(u_clut, vec2(idx, pc.clut_v));
    fragColor = vec4(color.rgb, color.a * pc.alpha);
}
)glsl";

// Glow pass (additive): decodes glow-encoding pixels, outputs additive RGB delta.
static constexpr const char* VK_KSPR_GLOW_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite;

const vec3 k_glow_step[8] = vec3[8](
    vec3(16.0, 16.0, 16.0) / 255.0,
    vec3(24.0, 16.0,  0.0) / 255.0,
    vec3(24.0,  4.0,  4.0) / 255.0,
    vec3( 8.0,  8.0, 24.0) / 255.0,
    vec3( 8.0, 24.0,  8.0) / 255.0,
    vec3(12.0,  0.0, 12.0) / 255.0,
    vec3( 0.0,  0.0,  0.0) / 255.0,
    vec3(24.0, 12.0,  4.0) / 255.0
);

void main()
{
    int px = int(texture(u_sprite, v_uv).r * 255.0 + 0.5);
    if (px < 1 || px > 64) discard;
    int code   = px - 1;
    int family = code / 8;
    int row    = code % 8;
    if (row == 0 || family == 6) discard;
    vec3 glow = clamp(k_glow_step[family] * float(row), 0.0, 1.0);
    fragColor = vec4(glow, 1.0);
}
)glsl";

// Depth-fail outline: draws owner-colour silhouette where sprite is occluded.
// set=0 binding=0 : R8 sprite
static constexpr const char* VK_KSPR_OUTLINE_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_sprite;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  outline_color;
} pc;

void main()
{
    float idx = texture(u_sprite, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = pc.outline_color;
}
)glsl";

/******************************************************************************/
// Shadow shaders
/******************************************************************************/

static constexpr const char* VK_SHADOW_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 0) out vec2 v_uv;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
} pc;

void main()
{
    vec2 ndc;
    ndc.x = a_pos.x / pc.screen_size.x * 2.0 - 1.0;
    ndc.y = 1.0 - a_pos.y / pc.screen_size.y * 2.0;
    gl_Position = vec4(ndc, pc.ndc_z_shadow, 1.0);
    v_uv = a_uv;
}
)glsl";

// set=0 binding=0 : R8 silhouette texture
static constexpr const char* VK_SHADOW_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_silhouette;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
} pc;

void main()
{
    float mask = texture(u_silhouette, v_uv).r;
    if (mask == 0.0) discard;
    fragColor = vec4(0.0, 0.0, 0.0, pc.alpha);
}
)glsl";

/******************************************************************************/
// Flat-colour polygon shaders (QK_PolyMode0, QK_PolyMode4, QK_BasicPolygon)
/******************************************************************************/

static constexpr const char* VK_FLATPOLY_VERT = R"glsl(
#version 450
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 0) out vec3 v_color;

layout(push_constant) uniform PC {
    vec2 screen_size;
} pc;

void main()
{
    float ndc_x = a_pos.x / pc.screen_size.x * 2.0 - 1.0;
    float ndc_y = 1.0 - a_pos.y / pc.screen_size.y * 2.0;
    gl_Position = vec4(ndc_x, ndc_y, a_pos.z, 1.0);
    v_color = a_color;
}
)glsl";

static constexpr const char* VK_FLATPOLY_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 fragColor;
void main() { fragColor = vec4(v_color, 1.0); }
)glsl";

/******************************************************************************/
// Post-process: passthrough (lens → swapchain blit)
/******************************************************************************/

static constexpr const char* VK_PASSTHROUGH_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_texture;

void main() { fragColor = texture(u_texture, v_uv); }
)glsl";

/******************************************************************************/
// Post-process: lens effects
/******************************************************************************/

// Displacement.
static constexpr const char* VK_LENS_DISPLACE_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_texture;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
    float magnitude;
    float period;
} pc;

void main()
{
    vec2 uv = v_uv;
    float dx = sin(uv.y * pc.period + pc.time) * pc.magnitude;
    float dy = cos(uv.x * pc.period + pc.time * 0.7) * pc.magnitude;
    uv += vec2(dx, dy);
    uv = clamp(uv, 0.0, 1.0);
    fragColor = texture(u_texture, uv);
}
)glsl";

// Mist.
static constexpr const char* VK_LENS_MIST_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_texture;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float mist_r;
    float mist_g;
    float mist_b;
    float mist_density;
} pc;

void main()
{
    vec4 scene = texture(u_texture, v_uv);
    float n = fract(sin(dot(v_uv + pc.time * 0.01, vec2(12.9898, 78.233))) * 43758.5453);
    float fog = smoothstep(0.3, 0.7, n) * pc.mist_density;
    fragColor = mix(scene, vec4(pc.mist_r, pc.mist_g, pc.mist_b, 1.0), fog * 0.4);
}
)glsl";

// Flyeye (compound eye).
static constexpr const char* VK_LENS_FLYEYE_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_texture;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float hex_size;
} pc;

void main()
{
    vec2 uv = v_uv;
    float aspect = pc.screen_size.x / pc.screen_size.y;
    vec2 hex_uv = uv * vec2(aspect, 1.0) / pc.hex_size;
    vec2 center = (floor(hex_uv) + 0.5) * pc.hex_size / vec2(aspect, 1.0);
    fragColor = texture(u_texture, center);
}
)glsl";

// Overlay (alpha composite).
// set=0 binding=0 : scene texture
// set=0 binding=1 : overlay texture
static constexpr const char* VK_LENS_OVERLAY_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_texture;
layout(set=0, binding=1) uniform sampler2D u_overlay;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
} pc;

void main()
{
    vec4 scene   = texture(u_texture, v_uv);
    vec4 overlay = texture(u_overlay, v_uv);
    fragColor = mix(scene, overlay, overlay.a * pc.alpha);
}
)glsl";

/******************************************************************************/
// Map fade transition
/******************************************************************************/

static constexpr const char* VK_MAP_FADE_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 0) out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_pos * 0.5 + 0.5;
}
)glsl";

// set=0 binding=0 : parchment RGBA snapshot
// set=0 binding=1 : world RGBA snapshot
static constexpr const char* VK_MAP_FADE_FRAG = R"glsl(
#version 450
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(set=0, binding=0) uniform sampler2D u_parchment;
layout(set=0, binding=1) uniform sampler2D u_world;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
} pc;

void main()
{
    float a6 = pc.map_step;
    float fx = v_uv.x;
    float fy = 1.0 - v_uv.y;
    const float xmax = 320.0;

    float wp   = 32.0 - a6;
    float uv_px = clamp(fx + wp * (4.0 - 8.0 * fx) / xmax, 0.0, 1.0);
    float uv_py = clamp(fy + wp * 4.0 * (1.0 - 2.0 * fy) / xmax, 0.0, 1.0);

    float ww   = a6;
    float uv_wx = clamp(fx + ww * (4.0 - 8.0 * fx) / xmax, 0.0, 1.0);
    float uv_wy = clamp(fy + ww * 4.0 * (1.0 - 2.0 * fy) / xmax, 0.0, 1.0);

    float samp_py = 1.0 - uv_py;
    float samp_wy = 1.0 - uv_wy;

    float f_parch = wp / 32.0;
    float f_world = ww / 32.0;

    vec3 c_parch = texture(u_parchment, vec2(uv_px, samp_py)).rgb * f_parch;
    vec3 c_world = texture(u_world,     vec2(uv_wx, samp_wy)).rgb * f_world;

    fragColor = vec4(clamp(c_parch + c_world, 0.0, 1.0), 1.0);
}
)glsl";

/******************************************************************************/
// Screen tint (flat colour fullscreen quad)
/******************************************************************************/

static constexpr const char* VK_SCREEN_TINT_VERT = R"glsl(
#version 450
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
)glsl";

static constexpr const char* VK_SCREEN_TINT_FRAG = R"glsl(
#version 450
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PC {
    vec2  screen_size;
    float z_ndc;
    float alpha;
    vec4  clip_rect;
    float clip_radius;
    float clip_screen_h;
    float remap_row;
    float tint_factor;
    vec2  center_map;
    vec2  screen_center;
    float zoom_scale;
    vec2  inv_map_size;
    float map_step;
    float fullbright;
    float ambient;
    float shade_scale;
    float shade_gamma;
    int   lighting_mode;
    int   darkness_mode;
    int   tile_filter;
    float missing_tile;
    float time;
    float fog_speed;
    float fog_density;
    float ndc_z_shadow;
    vec4  tint_color;
} pc;

void main()
{
    if (pc.clip_radius >= 0.0)
    {
        vec2 p       = vec2(gl_FragCoord.x, pc.clip_screen_h - gl_FragCoord.y);
        vec2 center  = (pc.clip_rect.xy + pc.clip_rect.zw) * 0.5;
        vec2 halfSz  = (pc.clip_rect.zw - pc.clip_rect.xy) * 0.5;
        vec2 d       = abs(p - center) - halfSz + pc.clip_radius;
        float dist   = length(max(d, 0.0)) - pc.clip_radius;
        if (dist > 0.0) discard;
    }
    fragColor = pc.tint_color;
}
)glsl";

/******************************************************************************/

#endif // RENDERER_VULKAN_ENABLED
#endif // VKSHADERS_H
