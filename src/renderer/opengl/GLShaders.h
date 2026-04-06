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
out vec2  v_uv;
out float v_shade;
void main()
{
    gl_Position = vec4(a_pos, 1.0);
    v_uv        = a_uv;
    v_shade     = a_shade;
}
)glsl";

constexpr const char* WORLD_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2  v_uv;
in float v_shade;
uniform sampler2D u_tile_atlas;  // R8 palette-index atlas (unit 0)
uniform sampler1D u_palette;     // RGBA8 256-entry palette (unit 1)
out vec4 fragColor;
void main()
{
    // Atlas stores raw palette indices in the R channel (normalised to [0,1]).
    // Look up the 8-bit index in the palette then apply the shade factor.
    float idx = texture(u_tile_atlas, v_uv).r;
    vec4  col = texture(u_palette, idx);
    fragColor = vec4(col.rgb * v_shade, 1.0);
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