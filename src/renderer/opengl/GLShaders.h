/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLShaders.h
 *     Embedded GLSL shader sources for the OpenGL renderer.
 *     All shaders are compiled into the executable for easier distribution.
 */
/******************************************************************************/
#pragma once

// Text rendering shaders
constexpr const char* TEXT_VERTEX_SHADER = R"glsl(
#version 330 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_forced_idx;

uniform vec2 u_viewport;
uniform vec4 u_text_color;

out vec2 v_uv;
out float v_forced_idx;

void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
    v_forced_idx = a_forced_idx;
}
)glsl";

constexpr const char* TEXT_FRAGMENT_SHADER = R"glsl(
#version 330 core

in vec2 v_uv;
in float v_forced_idx;

uniform sampler2D u_font_atlas;
uniform sampler2D u_palette;
uniform vec4 u_text_color;

out vec4 FragColor;

void main()
{
    vec4 atlas_sample = texture(u_font_atlas, v_uv);
    float alpha = atlas_sample.a;

    if (alpha < 0.1)
        discard;

    // v_forced_idx >= 0: Lb_TEXT_ONE_COLOR mode — use forced palette index.
    // v_forced_idx <  0: normal mode — use palette index from atlas red channel.
    float palette_u;
    if (v_forced_idx >= 0.0) {
        palette_u = (v_forced_idx + 0.5) / 256.0;
    } else {
        float raw_idx = atlas_sample.r * 255.0;
        palette_u = (raw_idx + 0.5) / 256.0;
    }

    vec3 palette_color = texture(u_palette, vec2(palette_u, 0.5)).rgb;
    vec3 final_color = palette_color * u_text_color.rgb;

    FragColor = vec4(final_color, alpha * u_text_color.a);
}
)glsl";

// Sprite rendering shaders
constexpr const char* KSPR_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2  u_viewport;
uniform float u_z_ndc;
out vec2 v_uv;
void main()
{
    vec2 ndc;
    ndc.x = a_pos.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - a_pos.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, u_z_ndc, 1.0);
    v_uv = a_uv;
}
)glsl";

constexpr const char* KSPR_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_sprite;    // GL_R8  palette-index map (256x256)
uniform sampler2D u_palette;   // GL_RGBA8 colour table  (256x1)
uniform float     u_alpha;     // 1.0=solid, 0.5=transpar4, 0.25=transpar8
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 color = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(color.rgb, u_alpha);
}
)glsl";

constexpr const char* KSPR_GLOW_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_sprite;
out vec4 fragColor;

// Per-row additive RGB step for each of the 8 glow families (8-bit normalised).
// Order: white, yellow, red, blue, green, purple, black/darken, orange.
// Values = (dR*4, dG*4, dB*4) / 255  where dR/dG/dB are from compute_alpha_tables().
const vec3 k_glow_step[8] = vec3[8](
    vec3(16.0, 16.0, 16.0) / 255.0,  // white:  dR=4, dG=4, dB=4
    vec3(24.0, 16.0,  0.0) / 255.0,  // yellow: dR=6, dG=4, dB=0
    vec3(24.0,  4.0,  4.0) / 255.0,  // red:    dR=6, dG=1, dB=1
    vec3( 8.0,  8.0, 24.0) / 255.0,  // blue:   dR=2, dG=2, dB=6
    vec3( 8.0, 24.0,  8.0) / 255.0,  // green:  dR=2, dG=6, dB=2
    vec3(12.0,  0.0, 12.0) / 255.0,  // purple: dR=3, dG=0, dB=3
    vec3( 0.0,  0.0,  0.0) / 255.0,  // black/darken — no additive contribution
    vec3(24.0, 12.0,  4.0) / 255.0   // orange: dR=6, dG=3, dB=1
);

void main()
{
    // Sprite pixels 1-64 are DK glow-encoding indices.
    // code = px-1; family = code/8 (0=white..7=orange); row = code%8 (intensity)
    // Row 0 = no glow; family 6 = darken (no additive contribution).
    // Drawn with GL_ONE / GL_ONE so the RGB delta is added directly to the framebuffer.
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

// Palette blit shaders
constexpr const char* PALETTE_BLIT_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)glsl";

constexpr const char* PALETTE_BLIT_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_index;    // R8 — 8-bit palette index
uniform sampler1D u_palette;  // RGBA8 — 256-entry palette
uniform float     u_tint_factor; // possession/pain red tint: 0.0=none 1.0=full red
void main()
{
    float idx        = texture(u_index, v_uv).r;
    float is_nonzero = step(0.5 / 256.0, idx);
    vec4  pal_color  = texture(u_palette, idx);
    pal_color.a = 1.0;
    // Index 0: transparent with optional red tint (possession/pain effect).
    // Non-zero: opaque palette color (GUI already has red-shifted palette).
    fragColor = mix(vec4(1.0, 0.0, 0.0, u_tint_factor), pal_color, is_nonzero);
}
)glsl";

// Screen-tint overlay shaders — simple flat-colour fullscreen quad.
// Used to composite palette effects (possession/pain tint, white flash) over
// all rendered layers (tiles, sprites, UI, text) in GPU world mode.
constexpr const char* SCREEN_TINT_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }
)glsl";

constexpr const char* SCREEN_TINT_FRAGMENT_SHADER = R"glsl(
#version 330 core
out vec4 fragColor;
uniform vec4 u_tint_color;
void main() { fragColor = u_tint_color; }
)glsl";

// Shadow shaders
constexpr const char* SHADOW_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_viewport;
out vec2 v_uv;
void main()
{
    vec2 ndc;
    ndc.x = a_pos.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - a_pos.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
}
)glsl";

constexpr const char* SHADOW_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_silhouette;
uniform float u_darkness;
out vec4 fragColor;
void main()
{
    float mask = texture(u_silhouette, v_uv).r;
    if (mask == 0.0) discard;
    // Blend equation GL_ZERO / GL_ONE_MINUS_SRC_ALPHA darkens the framebuffer:
    //   output = dst_color * (1.0 - darkness)
    fragColor = vec4(0.0, 0.0, 0.0, u_darkness);
}
)glsl";

// World rendering shaders
constexpr const char* WORLD_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in float a_shade;
layout(location = 3) in vec2  a_stl;   // subtile coords for lightmap (mode 1)
out vec2  v_uv;
out float v_shade;
out vec2  v_stl;
void main()
{
    gl_Position = vec4(a_pos, 1.0);
    v_uv        = a_uv;
    v_shade     = a_shade;
    v_stl       = a_stl;
}
)glsl";

constexpr const char* WORLD_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2  v_uv;
in float v_shade;
in vec2  v_stl;                         // subtile coords [0..511], mode 1 only
uniform sampler2D  u_tile_atlas;        // R8 palette-index atlas (unit 0)
uniform sampler1D  u_palette;           // RGBA8 256-entry palette (unit 1)
uniform usampler2D u_lightmap;          // R16UI subtile_lightness map (unit 2), mode 1
uniform float      u_fullbright;        // 0=normal shading, 1=bypass shade
uniform float      u_ambient;           // darkness floor added to shade [0,1]
uniform float      u_shade_scale;       // brightness multiplier (1.0=original)
uniform float      u_shade_gamma;       // shade curve exponent  (1.0=linear)
uniform int        u_lighting_mode;     // 0=software-accurate, 1=modern (Phase 3+)
uniform int        u_tile_filter;        // 0=nearest, 1=palette-correct bilinear
out vec4 fragColor;
void main()
{
    // Sampling the R8 atlas with GL_LINEAR would interpolate palette *indices*,
    // producing wrong colours.  For bilinear mode we instead manually sample the
    // 4 neighbouring texels (GL_NEAREST), look each up in the palette, then lerp
    // the resulting RGBA colours — palette-correct bilinear filtering.
    vec4 col;
    if (u_tile_filter == 1) {
        vec2 tex_size = vec2(textureSize(u_tile_atlas, 0));
        vec2 px   = v_uv * tex_size - 0.5;
        vec2 f    = fract(px);
        vec2 base = (floor(px) + 0.5) / tex_size;
        vec2 st   = 1.0 / tex_size;
        vec4 c00 = texture(u_palette, texture(u_tile_atlas, base).r);
        vec4 c10 = texture(u_palette, texture(u_tile_atlas, base + vec2(st.x, 0.0)).r);
        vec4 c01 = texture(u_palette, texture(u_tile_atlas, base + vec2(0.0, st.y)).r);
        vec4 c11 = texture(u_palette, texture(u_tile_atlas, base + st).r);
        col = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
    } else {
        col = texture(u_palette, texture(u_tile_atlas, v_uv).r);
    }
    float shade;
    if (u_lighting_mode == 0) {
        // Software-accurate: per-vertex Gouraud shade from the DK fade table.
        // The fade table reaches 100% brightness at row 32; rows 33-62 are
        // over-bright (clamped below).  v_shade = (S>>16) / 32.0.
        shade = mix(v_shade, 1.0, u_fullbright);
        shade = max(shade, u_ambient);
        shade = pow(clamp(shade * u_shade_scale, 0.0, 1.0), u_shade_gamma);
    } else {
        // Modern lighting: per-fragment lightmap sample (Phase 3 placeholder).
        // Samples the subtile_lightness map at normalised coords v_stl/511.
        // lightness range 0..16128: 16128 / (32*256) = 8192, so dividing by
        // 8192 gives ~1.97 for max brightness, clamped to 1.0 below.
        vec2 lm_uv  = v_stl / vec2(511.0, 511.0);
        uint lm_raw = texture(u_lightmap, lm_uv).r;
        shade = float(lm_raw) / 8192.0;
        shade = mix(shade, 1.0, u_fullbright);
        shade = max(shade, u_ambient);
        shade = pow(clamp(shade * u_shade_scale, 0.0, 1.0), u_shade_gamma);
    }
    fragColor = vec4(col.rgb * shade, 1.0);
}
)glsl";

// Flat-colour polygon shaders (QK_PolyMode0, QK_PolyMode4, QK_BasicPolygon)
constexpr const char* FLATPOLY_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec3 a_pos;    // x,y = screen pixel; z = NDC depth [-1,1]
layout(location = 1) in vec3 a_color;  // linear RGB [0,1]
uniform vec2 u_viewport;
out vec3 v_color;
void main()
{
    float ndc_x = a_pos.x / u_viewport.x * 2.0 - 1.0;
    float ndc_y = 1.0 - a_pos.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc_x, ndc_y, a_pos.z, 1.0);
    v_color = a_color;
}
)glsl";

constexpr const char* FLATPOLY_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec3 v_color;
out vec4 fragColor;
void main()
{
    fragColor = vec4(v_color, 1.0);
}
)glsl";

// UI rendering shaders
// Shared vertex shader — same VAO layout for all three UI programs.
constexpr const char* UI_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;      // Screen position
layout(location = 1) in vec2 a_uv;       // Texture coordinates
layout(location = 2) in vec4 a_color;    // RGBA color/tint
layout(location = 3) in float a_z;       // Z-depth for sorting
layout(location = 4) in float a_mode;    // Render mode (unused in per-type programs)

uniform vec2 u_screen_size;

out vec2 v_uv;
out vec4 v_color;

void main()
{
    vec2 ndc;
    ndc.x = (a_pos.x / u_screen_size.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (a_pos.y / u_screen_size.y) * 2.0;
    gl_Position = vec4(ndc, a_z, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)glsl";

// Program 1: palette-indexed atlas sprites (panel icons, buttons, minimap).
// Unit 0 = sprite atlas (R8 GL_TEXTURE_2D).  Unit 1 = palette (sampler1D).
constexpr const char* UI_SPRITE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;  // unit 0: R8 palette-index atlas
uniform sampler1D u_palette;       // unit 1: RGBA8 256-entry palette
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 pal = texture(u_palette, idx);
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";

// Program 2: font glyph rendering.
// Unit 0 = font atlas (RGBA GL_TEXTURE_2D); alpha channel is the glyph mask.
constexpr const char* UI_FONT_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_font_atlas;  // unit 0: font glyph RGBA texture
out vec4 fragColor;
void main()
{
    float alpha = texture(u_font_atlas, v_uv).a;
    if (alpha < 0.1) discard;
    fragColor = vec4(v_color.rgb, alpha * v_color.a);
}
)glsl";

// Program 3: solid-colour quads and lines (slab selectors, solid boxes).
// No textures.
constexpr const char* UI_SOLID_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec4 v_color;
out vec4 fragColor;
void main()
{
    fragColor = v_color;
}
)glsl";

// Program 5: palette-indexed atlas sprites used as a discard mask; outputs flat vertex colour.
// Unit 0 = sprite atlas (R8 GL_TEXTURE_2D).
// Transparent pixels (index 0) are discarded; all opaque pixels output v_color directly.
constexpr const char* UI_SPRITE_COLORED_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = v_color;
}
)glsl";

// Program 4: palette-indexed atlas sprites with a player-colour remap table.
// Unit 0 = sprite atlas (R8 GL_TEXTURE_2D).
// Unit 1 = palette (sampler1D, 256 RGBA entries).
// Unit 2 = fade table (R8 GL_TEXTURE_2D, 256 columns × 256 rows).
// uniform u_remap_row selects which row of the fade table to apply.
constexpr const char* UI_REMAP_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;  // unit 0: R8 palette-index atlas
uniform sampler1D u_palette;       // unit 1: RGBA8 256-entry palette
uniform sampler2D u_fade_table;    // unit 2: R8 256x256 remap LUT
uniform float u_remap_row;         // 0..255 — which row of the fade table
out vec4 fragColor;
void main()
{
    float idx_f = texture(u_sprite_atlas, v_uv).r;
    if (idx_f < (0.5 / 255.0)) discard;
    float remap_y = (u_remap_row + 0.5) / 256.0;
    float remapped_f = texture(u_fade_table, vec2(idx_f, remap_y)).r;
    vec4 pal = texture(u_palette, remapped_f);
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";