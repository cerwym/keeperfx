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
    // v_uv.x < 0: solid-colour underline rect — no atlas lookup needed.
    if (v_uv.x < 0.0) {
        float palette_u = (v_forced_idx + 0.5) / 256.0;
        vec3 palette_color = texture(u_palette, vec2(palette_u, 0.5)).rgb;
        FragColor = vec4(palette_color * u_text_color.rgb, u_text_color.a);
        return;
    }

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

// Atlas variant: u_sprite is a GL_TEXTURE_2D_ARRAY;
// u_layer selects the pre-decoded layer for this sprite.
constexpr const char* KSPR_ARRAY_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2DArray u_sprite;   // GL_R8 texture array, one layer per unique sprite
uniform sampler2D      u_clut;     // GL_RGBA8 256xN CLUT — row 0=identity, rows 1..N-1=remaps
uniform float          u_alpha;    // 1.0=solid, 0.5=transpar4, 0.25=transpar8
uniform float          u_layer;    // layer index in the sprite array
uniform float          u_clut_v;   // V texcoord selecting the CLUT row
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite, vec3(v_uv, u_layer)).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 color = texture(u_clut, vec2(idx, u_clut_v));
    fragColor = vec4(color.rgb, color.a * u_alpha);
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

// Depth-fail outline shaders — draw a flat owner-colour silhouette only where
// the creature sprite is occluded by geometry (depth test = GL_GREATER).
constexpr const char* KSPR_OUTLINE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_sprite;
uniform vec4      u_outline_color;
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = u_outline_color;
}
)glsl";

// Array-atlas variant of the outline shader (sampler2DArray).
constexpr const char* KSPR_ARRAY_OUTLINE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2DArray u_sprite;
uniform float          u_layer;
uniform vec4           u_outline_color;
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite, vec3(v_uv, u_layer)).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = u_outline_color;
}
)glsl";

// Edge-detect variant of the outline shader (sampler2D).
// Emits outline colour only at sprite boundary pixels (where at least one
// cardinal neighbour has palette index 0).  Texel step is 1/256 — the atlas
// tile dimension is compile-time fixed at 256×256.
constexpr const char* KSPR_EDGE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_sprite;
uniform vec4      u_outline_color;
out vec4 fragColor;
const float kStep = 1.0 / 256.0;
const float kThr  = 0.5 / 255.0;
void main()
{
    float idx = texture(u_sprite, v_uv).r;
    if (idx < kThr) discard;
    float l = texture(u_sprite, v_uv + vec2(-kStep, 0.0)).r;
    float r = texture(u_sprite, v_uv + vec2( kStep, 0.0)).r;
    float u = texture(u_sprite, v_uv + vec2(0.0, -kStep)).r;
    float d = texture(u_sprite, v_uv + vec2(0.0,  kStep)).r;
    if (l >= kThr && r >= kThr && u >= kThr && d >= kThr) discard;
    fragColor = u_outline_color;
}
)glsl";

// Edge-detect variant — array-atlas (sampler2DArray + u_layer).
constexpr const char* KSPR_ARRAY_EDGE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2DArray u_sprite;
uniform float          u_layer;
uniform vec4           u_outline_color;
out vec4 fragColor;
const float kStep = 1.0 / 256.0;
const float kThr  = 0.5 / 255.0;
void main()
{
    float idx = texture(u_sprite, vec3(v_uv, u_layer)).r;
    if (idx < kThr) discard;
    float l = texture(u_sprite, vec3(v_uv + vec2(-kStep, 0.0), u_layer)).r;
    float r = texture(u_sprite, vec3(v_uv + vec2( kStep, 0.0), u_layer)).r;
    float u = texture(u_sprite, vec3(v_uv + vec2(0.0, -kStep), u_layer)).r;
    float d = texture(u_sprite, vec3(v_uv + vec2(0.0,  kStep), u_layer)).r;
    if (l >= kThr && r >= kThr && u >= kThr && d >= kThr) discard;
    fragColor = u_outline_color;
}
)glsl";

// Array-atlas variant of the glow shader — additive sprites cached in atlas.
constexpr const char* KSPR_ARRAY_GLOW_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2DArray u_sprite;
uniform float          u_layer;
out vec4 fragColor;

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
    int px = int(texture(u_sprite, vec3(v_uv, u_layer)).r * 255.0 + 0.5);
    if (px < 1 || px > 64) discard;
    int code   = px - 1;
    int family = code / 8;
    int row    = code % 8;
    if (row == 0 || family == 6) discard;
    vec3 glow = clamp(k_glow_step[family] * float(row), 0.0, 1.0);
    fragColor = vec4(glow, 1.0);
}
)glsl";

// ── Instanced keeper-sprite shaders ──────────────────────────────────────────
// One glDrawArraysInstanced call draws every atlas-resident sprite of the
// frame: the unit quad (location 0) is expanded per instance by a_rect, and
// all per-sprite state (atlas layer, CLUT row, alpha, depth, flip/additive
// flags) rides in per-instance attributes instead of uniforms.
//
// Blending is unified as premultiplied alpha — glBlendFunc(GL_ONE,
// GL_ONE_MINUS_SRC_ALPHA) — so normal sprites (rgb*a, a) and additive glow
// sprites (rgb, 0) coexist in a single draw with no blend-state changes.
constexpr const char* KSPR_INST_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_corner;  // unit quad corner, (0,0)..(1,1)
layout(location = 1) in vec4 a_rect;    // instance: dst x, y, w, h (screen px)
layout(location = 2) in vec2 a_uvext;   // instance: uv extent (src_w/dim, src_h/dim)
layout(location = 3) in vec4 a_misc;    // instance: layer, clut_v, alpha, z_ndc
layout(location = 4) in uint a_flags;   // instance: bit0 = flip_h, bit1 = additive
uniform vec2 u_viewport;
out vec2 v_uv;
flat out vec3 v_lca;    // layer, clut_v, alpha
flat out uint v_flags;
void main()
{
    vec2 px = a_rect.xy + a_corner * a_rect.zw;
    vec2 ndc;
    ndc.x = px.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - px.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, a_misc.w, 1.0);
    float u = ((a_flags & 1u) != 0u) ? (1.0 - a_corner.x) : a_corner.x;
    v_uv    = vec2(u * a_uvext.x, a_corner.y * a_uvext.y);
    v_lca   = a_misc.xyz;
    v_flags = a_flags;
}
)glsl";

constexpr const char* KSPR_INST_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
flat in vec3 v_lca;    // layer, clut_v, alpha
flat in uint v_flags;  // bit1 = additive glow
uniform sampler2DArray u_sprite;   // GL_R8 decode atlas, one layer per sprite
uniform sampler2D      u_clut;     // 256xN CLUT — row 0 identity, rows 1..N remaps
out vec4 fragColor;

// Same glow families/steps as KSPR_GLOW_FRAGMENT_SHADER (see there for docs).
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
    float idx = texture(u_sprite, vec3(v_uv, v_lca.x)).r;
    if ((v_flags & 2u) != 0u)
    {
        // Additive glow: alpha 0 makes (ONE, ONE_MINUS_SRC_ALPHA) act as (ONE, ONE).
        int px = int(idx * 255.0 + 0.5);
        if (px < 1 || px > 64) discard;
        int code   = px - 1;
        int family = code / 8;
        int row    = code % 8;
        if (row == 0 || family == 6) discard;
        vec3 glow = clamp(k_glow_step[family] * float(row), 0.0, 1.0);
        fragColor = vec4(glow, 0.0);
    }
    else
    {
        if (idx < (0.5 / 255.0)) discard;
        vec4 c = texture(u_clut, vec2(idx, v_lca.y));
        float a = c.a * v_lca.z;
        fragColor = vec4(c.rgb * a, a);  // premultiplied
    }
}
)glsl";

// Instanced depth-fail outline: flat owner colour where the sprite is behind
// geometry (drawn with glDepthFunc(GL_GREATER) before the main instanced pass).
constexpr const char* KSPR_INST_OUTLINE_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec4 a_rect;
layout(location = 2) in vec2 a_uvext;
layout(location = 3) in vec3 a_lzf;     // instance: layer, z_ndc, flip (0/1)
layout(location = 4) in vec4 a_color;   // instance: outline rgba
uniform vec2 u_viewport;
out vec2 v_uv;
flat out float v_layer;
flat out vec4  v_color;
void main()
{
    vec2 px = a_rect.xy + a_corner * a_rect.zw;
    vec2 ndc;
    ndc.x = px.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - px.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, a_lzf.y, 1.0);
    float u = (a_lzf.z > 0.5) ? (1.0 - a_corner.x) : a_corner.x;
    v_uv    = vec2(u * a_uvext.x, a_corner.y * a_uvext.y);
    v_layer = a_lzf.x;
    v_color = a_color;
}
)glsl";

constexpr const char* KSPR_INST_OUTLINE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
flat in float v_layer;
flat in vec4  v_color;
uniform sampler2DArray u_sprite;
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite, vec3(v_uv, v_layer)).r;
    if (idx < (0.5 / 255.0)) discard;
    fragColor = vec4(v_color.rgb * v_color.a, v_color.a);  // premultiplied
}
)glsl";

// Instanced edge-detect: only emit colour at sprite boundary pixels.
// Shares the same vertex shader and VAO as the instanced outline.
constexpr const char* KSPR_INST_EDGE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
flat in float v_layer;
flat in vec4  v_color;
uniform sampler2DArray u_sprite;
out vec4 fragColor;
const float kStep = 1.0 / 256.0;
const float kThr  = 0.5 / 255.0;
void main()
{
    float idx = texture(u_sprite, vec3(v_uv, v_layer)).r;
    if (idx < kThr) discard;
    float l = texture(u_sprite, vec3(v_uv + vec2(-kStep, 0.0), v_layer)).r;
    float r = texture(u_sprite, vec3(v_uv + vec2( kStep, 0.0), v_layer)).r;
    float u = texture(u_sprite, vec3(v_uv + vec2(0.0, -kStep), v_layer)).r;
    float d = texture(u_sprite, vec3(v_uv + vec2(0.0,  kStep), v_layer)).r;
    if (l >= kThr && r >= kThr && u >= kThr && d >= kThr) discard;
    fragColor = vec4(v_color.rgb * v_color.a, v_color.a);  // premultiplied
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
uniform sampler2D u_palette;  // RGBA8 — 256×1 palette
uniform float     u_tint_factor; // possession/pain red tint: 0.0=none 1.0=full red
void main()
{
    float idx        = texture(u_index, v_uv).r;
    float is_nonzero = step(0.5 / 256.0, idx);
    vec4  pal_color  = texture(u_palette, vec2(idx, 0.5));
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
uniform vec4  u_tint_color;
uniform vec4  u_clip_rect;     // (x0, y0_gl, x1, y1_gl) in GL pixels; ignored when u_clip_radius < 0
uniform float u_clip_radius;   // corner radius in pixels; < 0 = no clip
uniform float u_clip_screen_h; // screen height for Y flip
void main()
{
    if (u_clip_radius >= 0.0)
    {
        vec2 p = vec2(gl_FragCoord.x, u_clip_screen_h - gl_FragCoord.y);
        vec2 center  = (u_clip_rect.xy + u_clip_rect.zw) * 0.5;
        vec2 halfSz  = (u_clip_rect.zw - u_clip_rect.xy) * 0.5;
        vec2 d = abs(p - center) - halfSz + u_clip_radius;
        float dist = length(max(d, 0.0)) - u_clip_radius;
        if (dist > 0.0) discard;
    }
    fragColor = u_tint_color;
}
)glsl";

// Shadow shaders
constexpr const char* SHADOW_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
uniform vec2 u_viewport;
uniform float u_ndc_z;     // NDC depth from the shadow's floor bucket
out vec2 v_uv;
void main()
{
    vec2 ndc;
    ndc.x = a_pos.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - a_pos.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, u_ndc_z, 1.0);
    v_uv = a_uv;
}
)glsl";

constexpr const char* SHADOW_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_silhouette;
uniform float u_darkness;       // per-shadow distance-based alpha (scaled by shadow_darkness_scale)
uniform vec4  u_shadow_colour;  // rgb=tint colour, a=intensity multiplier (from RendererSettings)
out vec4 fragColor;
void main()
{
    float mask = texture(u_silhouette, v_uv).r;
    if (mask == 0.0) discard;
    // Standard alpha blend (GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA).
    // When shadow_colour = (0,0,0,1) this is identical to the original multiply-darken:
    //   result = black * alpha + dst * (1 - alpha) = dst * (1 - alpha)
    fragColor = vec4(u_shadow_colour.rgb, u_darkness * u_shadow_colour.a);
}
)glsl";

// ── Instanced shadow shaders ─────────────────────────────────────────────────
// One glDrawArraysInstanced draws every creature shadow of the frame.  The
// quad is a sheared 4-corner polygon, so instead of a rect the instance
// carries all four corner positions and UVs packed into vec4 pairs; the
// vertex shader picks the corner from gl_VertexID (6 verts = 2 triangles,
// same 0,1,2 / 0,2,3 split as the legacy path).  Silhouette pixels come from
// a per-variant decode cache (GL_TEXTURE_2D_ARRAY) instead of being RLE
// decoded and re-uploaded per shadow per frame; circle shadows sample the
// pre-baked gradient on unit 1, selected by the is_circle instance flag.
constexpr const char* SHADOW_INST_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec4 a_c01;   // instance: corner0.xy, corner1.xy (screen px)
layout(location = 1) in vec4 a_c23;   // instance: corner2.xy, corner3.xy
layout(location = 2) in vec4 a_uv01;  // instance: uv0, uv1
layout(location = 3) in vec4 a_uv23;  // instance: uv2, uv3
layout(location = 4) in vec4 a_ldc;   // instance: layer, darkness, ndc_z, is_circle
uniform vec2 u_viewport;
out vec2 v_uv;
flat out vec3 v_ldc;   // layer, darkness, is_circle
void main()
{
    int corner;
    switch (gl_VertexID) {
        case 0:  corner = 0; break;
        case 1:  corner = 1; break;
        case 2:  corner = 2; break;
        case 3:  corner = 0; break;
        case 4:  corner = 2; break;
        default: corner = 3; break;
    }
    vec2 pos, uv;
    if      (corner == 0) { pos = a_c01.xy; uv = a_uv01.xy; }
    else if (corner == 1) { pos = a_c01.zw; uv = a_uv01.zw; }
    else if (corner == 2) { pos = a_c23.xy; uv = a_uv23.xy; }
    else                  { pos = a_c23.zw; uv = a_uv23.zw; }
    vec2 ndc;
    ndc.x = pos.x / u_viewport.x * 2.0 - 1.0;
    ndc.y = 1.0 - pos.y / u_viewport.y * 2.0;
    gl_Position = vec4(ndc, a_ldc.z, 1.0);
    v_uv  = uv;
    v_ldc = vec3(a_ldc.x, a_ldc.y, a_ldc.w);
}
)glsl";

constexpr const char* SHADOW_INST_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
flat in vec3 v_ldc;    // layer, darkness, is_circle
uniform sampler2DArray u_silhouettes;  // unit 0: cached silhouette masks
uniform sampler2D      u_circle;       // unit 1: pre-baked radial gradient
uniform vec4 u_shadow_colour;          // rgb=tint, a=intensity (RendererSettings)
out vec4 fragColor;
void main()
{
    float mask = (v_ldc.z > 0.5)
        ? texture(u_circle, v_uv).r
        : texture(u_silhouettes, vec3(v_uv, v_ldc.x)).r;
    if (mask == 0.0) discard;
    // Same blend semantics as SHADOW_FRAGMENT_SHADER: mask gates coverage,
    // alpha carries the per-shadow darkness.
    fragColor = vec4(u_shadow_colour.rgb, v_ldc.y * u_shadow_colour.a);
}
)glsl";

// World rendering shaders
constexpr const char* WORLD_VERTEX_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec2  a_uv;
layout(location = 2) in float a_shade;
layout(location = 3) in vec2  a_stl;       // subtile coords for lightmap (mode 1)
layout(location = 4) in float a_camera_z;  // camera-space Z for perspective correction
layout(location = 5) in float a_layer;     // texture array layer (atlas variation)
layout(location = 6) in vec3  aWorldPos;   // pre-projection world-space position
out vec2  v_uv;
out float v_shade;
out vec2  v_stl;
flat out float v_layer;
out vec3  vWorldPos;
void main()
{
    // Perspective-correct interpolation trick: multiply clip-space position by
    // camera_z (= gl_Position.w).  The rasterizer divides by w, restoring the
    // original NDC position, but now it also perspective-corrects all varyings
    // (v_uv, v_shade, v_stl) using the per-vertex w values.
    // When camera_z == 1.0 (unknown depth), this degrades to affine (no-op).
    float w = max(a_camera_z, 1.0);
    gl_Position = vec4(a_pos.xy * w, a_pos.z * w, w);
    v_uv        = a_uv;
    v_shade     = a_shade;
    v_stl       = a_stl;
    v_layer     = a_layer;
    vWorldPos   = aWorldPos;
}
)glsl";

constexpr const char* WORLD_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2  v_uv;
in float v_shade;
in vec2  v_stl;                         // subtile coords [0..511], mode 1 only
flat in float v_layer;                  // texture array layer (atlas variation)
in vec3  vWorldPos;                     // reserved for future dynamic lighting / shadow mapping
uniform sampler2DArray u_tile_atlas;    // R8 palette-index atlas array (unit 0)
uniform sampler2D  u_palette;           // RGBA8 256×1 palette (unit 1)
uniform usampler2D u_lightmap;          // R16UI subtile_lightness map (unit 2), mode 1
uniform sampler2D  u_fade_table;        // R8 256×256 fade/remap LUT (unit 3), palette mode
uniform float      u_fullbright;        // 0=normal shading, 1=bypass shade
uniform float      u_ambient;           // darkness floor added to shade [0,1]
uniform float      u_shade_scale;       // brightness multiplier (1.0=original)
uniform float      u_shade_gamma;       // shade curve exponent  (1.0=linear)
uniform int        u_lighting_mode;     // 0=software-accurate, 1=modern (Phase 3+)
uniform int        u_darkness_mode;     // 0=linear, 1=palette LUT, 2=animated fog
uniform int        u_tile_filter;       // 0=nearest, 1=palette-correct bilinear
uniform float      u_missing_tile;      // 1.0 = no valid atlas bound — show diagnostic
uniform float      u_time;             // seconds since start (fog animation)
uniform float      u_fog_speed;        // fog scroll speed multiplier
uniform float      u_fog_density;      // fog opacity [0,1]
out vec4 fragColor;

// Simple 2D hash for procedural noise (fog mode)
float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// Value noise with smooth interpolation
float noise2d(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // smoothstep
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Fractal Brownian Motion — 3 octaves
float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 3; i++)
    {
        v += a * noise2d(p);
        p *= 2.0;
        a *= 0.5;
    }
    return v;
}

void main()
{
    // When the tile atlas slot is missing (atlas not yet loaded or variation out of
    // range), show a magenta/black checkerboard so the problem is immediately visible
    // rather than a silent black tile.
    if (u_missing_tile > 0.5)
    {
        ivec2 cell = ivec2(gl_FragCoord.xy / 8.0);
        fragColor  = ((cell.x + cell.y) & 1) == 0
                     ? vec4(1.0, 0.0, 1.0, 1.0)  // magenta
                     : vec4(0.0, 0.0, 0.0, 1.0);  // black
        return;
    }

    // --- Compute raw shade (fade table row) ---
    // v_shade = (S>>16) / 32.0 where S = shade_intensity<<8.
    // This directly maps to fade table rows: 0.0 = row 0 (black),
    // 1.0 = row 32 (full brightness).  Used as-is for palette LUT modes.
    float raw_shade;
    if (u_lighting_mode == 0) {
        raw_shade = mix(v_shade, 1.0, u_fullbright);
    } else {
        vec2 lm_uv  = v_stl / vec2(511.0, 511.0);
        uint lm_raw = texture(u_lightmap, lm_uv).r;
        raw_shade = float(lm_raw) / 8192.0;
        raw_shade = mix(raw_shade, 1.0, u_fullbright);
    }

    // Pre-compute fade_v for palette-mode darkness so bilinear path can use it
    // per-neighbour without repeating the arithmetic.
    // Only valid when u_darkness_mode != 0 (avoids unit-3 access before load).
    float fade_v = 0.0;
    if (u_darkness_mode != 0)
    {
        float fade_row = raw_shade * 32.0;
        fade_v = (clamp(fade_row, 0.0, 63.0) + 0.5) / 256.0;
    }

    // --- Sample palette index from atlas (always needed) ---
    float pal_idx;  // raw palette index [0,1] from R8 atlas (nearest or non-bilinear)
    vec4  col;      // decoded RGBA colour (after palette lookup + optional shading)

    if (u_tile_filter == 1) {
        // Palette-correct bilinear: sample 4 neighbours, decode each, then lerp.
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

        if (u_darkness_mode != 0) {
            // Palette-mode darkness: shade each neighbour through the fade table
            // independently before blending.  This prevents shading discontinuities
            // at tile boundaries (the "crawling" artifact seen when idx00 alone was
            // used for the LUT, snapping to a new palette row on each texel crossing).
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
        pal_idx = idx00;  // kept for LINEAR shade multiply below
    } else {
        pal_idx = texture(u_tile_atlas, vec3(v_uv, v_layer)).r;
        col = texture(u_palette, vec2(pal_idx, 0.5));
    }

    // Palette index 0 = void/transparent in DK1 paletted data.
    // The same convention applies here as in the sprite/UI shader (which also discards index 0).
    // Without this, world-geometry tiles whose data is all-zero bytes (e.g. the entrance-portal
    // centre column, or unclaimed portal-floor tiles whose atlas variation is empty) render as
    // a solid opaque black quad, which is visually wrong.  Making them transparent is correct:
    // the portal centre is a gateway void, and unset tiles should not occlude anything beneath.
    if (pal_idx < 0.5 / 255.0)
        discard;

    // --- Palette fade-table lookup (PALETTE and FOG modes, nearest path only) ---
    // In the bilinear path (u_tile_filter == 1) each neighbour is already shaded
    // above, so col already contains the fully shaded and blended colour.
    // In the nearest path we do the LUT lookup here.
    // Only sampled when darkness_mode != LINEAR: avoids reading unit 3 when the
    // fade-table texture may not yet be loaded, which would produce undefined colours.
    // Replicates the software renderer's pixmap.fade_tables[] exactly:
    //   fade_tables[row * 256 + palette_index] → remapped palette index.
    // Row 0 = fully dark, row 32 = full brightness (identity).
    // The texture is 256 wide × 256 tall; first 64 rows are fade data.
    float remapped = pal_idx;      // fallback for LINEAR mode: identity remap
    vec3 pal_color = col.rgb;      // fallback for LINEAR mode: direct colour
    if (u_darkness_mode != 0 && u_tile_filter != 1)
    {
        remapped   = texture(u_fade_table, vec2(pal_idx, fade_v)).r;
        pal_color  = texture(u_palette,    vec2(remapped, 0.5)).rgb;
    }
    else if (u_darkness_mode != 0)
    {
        // Bilinear path: col already contains the blended shaded colour.
        // pal_color mirrors col for the fog/overlay code below.
        pal_color = col.rgb;
    }

    // --- Apply darkness mode ---
    if (u_darkness_mode == 1)
    {
        // Palette LUT: exact software renderer output — no further processing.
        fragColor = vec4(pal_color, 1.0);
    }
    else if (u_darkness_mode == 2)
    {
        // Animated fog: uses the palette LUT as the base colour (preserving
        // the authentic green-tinted darkness), then overlays scrolling fog
        // in dark areas for atmosphere.
        float fog_mask = 1.0 - clamp(raw_shade, 0.0, 1.0);

        if (fog_mask > 0.01)
        {
            vec2 fog_uv = gl_FragCoord.xy * 0.008
                        + vec2(u_time * u_fog_speed * 0.4,
                               u_time * u_fog_speed * 0.25);
            float n = fbm(fog_uv);
            vec3 fog_color = vec3(0.06, 0.10, 0.06);
            float fog_alpha = smoothstep(0.25, 0.65, n) * fog_mask * u_fog_density;
            fragColor = vec4(mix(pal_color, fog_color, fog_alpha), 1.0);
        }
        else
        {
            fragColor = vec4(pal_color, 1.0);
        }
    }
    else
    {
        // LINEAR: classic linear RGB multiply with tuning knobs.
        float shade = max(raw_shade, u_ambient);
        shade = pow(clamp(shade * u_shade_scale, 0.0, 1.0), u_shade_gamma);
        fragColor = vec4(col.rgb * shade, 1.0);
    }
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

// Passthrough blit shader — copies a texture to the screen (or another FBO).
// Used to blit the final lens-processed scene to the default framebuffer.
constexpr const char* PASSTHROUGH_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
void main()
{
    fragColor = texture(u_texture, v_uv);
}
)glsl";

// scRGB gamma lift shader — sRGB-encoded game output → linear scRGB.
// Applied as the final presentation pass when the backbuffer is a linear float
// surface (SDL_GL_FLOATBUFFERS on HDR displays).  Without this pass DWM would
// interpret the sRGB-encoded values as linear light, making the image ~2× too
// bright.  Uses the exact IEC 61966-2-1 transfer function.
constexpr const char* SCRGB_LIFT_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
vec3 srgb_to_linear(vec3 c) {
    bvec3 lo = lessThanEqual(c, vec3(0.04045));
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, vec3(lo));
}
void main() {
    fragColor = vec4(srgb_to_linear(texture(u_texture, v_uv).rgb), 1.0);
}
)glsl";

// ── GPU Lens Effect Shaders ──────────────────────────────────────────────────

// Displacement lens: distorts the scene using a sinusoidal displacement map.
constexpr const char* LENS_DISPLACEMENT_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_time;
uniform float u_magnitude;
uniform float u_period;
void main()
{
    vec2 uv = v_uv;
    float dx = sin(uv.y * u_period + u_time) * u_magnitude;
    float dy = cos(uv.x * u_period + u_time * 0.7) * u_magnitude;
    uv += vec2(dx, dy);
    uv = clamp(uv, 0.0, 1.0);
    fragColor = texture(u_texture, uv);
}
)glsl";

// Mist lens: blends a fog colour over the scene based on a noise pattern.
constexpr const char* LENS_MIST_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_time;
uniform vec4  u_mist_color;  // RGB + density
void main()
{
    vec4 scene = texture(u_texture, v_uv);
    // Simple animated noise-based fog
    float n = fract(sin(dot(v_uv + u_time * 0.01, vec2(12.9898, 78.233))) * 43758.5453);
    float fog = smoothstep(0.3, 0.7, n) * u_mist_color.a;
    fragColor = mix(scene, vec4(u_mist_color.rgb, 1.0), fog * 0.4);
}
)glsl";

// Flyeye lens: compound eye (hexagonal tiling) post-process effect.
constexpr const char* LENS_FLYEYE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;
uniform float u_hex_size;     // hexagon size in UV space
uniform vec2  u_resolution;   // screen resolution
void main()
{
    vec2 uv = v_uv;
    // Hexagonal grid quantisation
    float aspect = u_resolution.x / u_resolution.y;
    vec2 hex_uv = uv * vec2(aspect, 1.0) / u_hex_size;
    vec2 center = (floor(hex_uv) + 0.5) * u_hex_size / vec2(aspect, 1.0);
    // Sample from hex cell center
    fragColor = texture(u_texture, center);
}
)glsl";

// Overlay lens: composites an overlay texture with alpha blending.
constexpr const char* LENS_OVERLAY_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_texture;     // scene
uniform sampler2D u_overlay;     // overlay image
uniform float u_overlay_alpha;   // blend factor
void main()
{
    vec4 scene   = texture(u_texture, v_uv);
    vec4 overlay = texture(u_overlay, v_uv);
    fragColor = mix(scene, overlay, overlay.a * u_overlay_alpha);
}
)glsl";

// Raw-image blit fragment shader.
// Used by RendererOpenGL::BlitRaw8GPU() for frontend background images
// (legal screen, loading screen, menu background, map background, etc.).
// Unlike the staging-buffer palette blit, all pixels are fully opaque —
// palette index 0 is treated as a normal colour, not transparency.
// The vertex shader is shared with the staging blit (PALETTE_BLIT_VERTEX_SHADER).
constexpr const char* RAWIMAGE_BLIT_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_index;    // R8  — 8-bit palette indices (raw image)
uniform sampler2D u_palette;  // RGBA8 — 256×1 palette (same as staging blit)
void main()
{
    float idx = texture(u_index, v_uv).r;
    vec4  pal = texture(u_palette, vec2(idx, 0.5));
    // Always opaque: raw background images fill the entire rect.
    fragColor  = vec4(pal.rgb, 1.0);
}
)glsl";

// Overhead map tile fragment shader.
// Input texture is RG8:
//   R = palette index (direct) or ghost table texture row (ghost-shaded).
//   G = operation type:
//     0x00 = transparent (unrevealed — parchment shows through)
//     0xFF = direct palette lookup on R
//     0x01 = simple ghost:  palette[ ghost[R_row, parchment_col] ]
//     0x02 = gems ghost:    palette[ 102 + (ghost[64, parchment_col] >> 6) ]
//     0x03 = blink+gems:    palette[ ghost[R_row, parchment_col] + 2 ]
// Ghost table rows in the fade texture are offset by +64 (rows 0-63 = fade).
// Blending must be enabled by the caller.
constexpr const char* OVERHEAD_MAP_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_index;      // RG8 — tile data (R=value, G=operation)
uniform sampler2D u_palette;    // RGBA8 — 256×1 game palette
uniform sampler2D u_fade;       // R8 256×256 — fade/ghost table
uniform sampler2D u_parchment;  // R8 — parchment background (palette indices)
uniform vec4  u_map_rect;       // (x0, y0, x1, y1) overhead map screen rect
uniform vec2  u_screen_size;    // (width, height) for Y flip + parchment UV

void main()
{
    vec4 tile = texture(u_index, v_uv);
    float r_raw = tile.r;   // R channel: palette index or ghost row
    float g_raw = tile.g;   // G channel: operation type

    // G ≈ 0 → transparent (unrevealed)
    if (g_raw < (0.5 / 255.0)) discard;

    int op = int(g_raw * 255.0 + 0.5);
    int r_val = int(r_raw * 255.0 + 0.5);

    float pal_idx;
    if (op == 255)
    {
        // Direct palette lookup
        pal_idx = r_raw;
    }
    else
    {
        // Ghost-table operation: sample parchment pixel at this fragment's
        // screen position to get the background palette index.
        // Parchment rawblit covers (0,0)–(screen_w,screen_h), so UV = frag / screen_size.
        vec2 frag = vec2(gl_FragCoord.x, u_screen_size.y - gl_FragCoord.y);
        vec2 parch_uv = frag / u_screen_size;
        parch_uv = clamp(parch_uv, 0.0, 1.0);
        float bg_idx = texture(u_parchment, parch_uv).r;
        // bg_idx is in [0,1]; represents palette index 0-255.

        if (op == 1)
        {
            // Simple ghost: ghost_table[row, bg_col] → palette index
            float ghost_row = (float(r_val) + 0.5) / 256.0;
            float ghost_col = bg_idx;  // already normalised
            pal_idx = texture(u_fade, vec2(ghost_col, ghost_row)).r;
        }
        else if (op == 2)
        {
            // Gems: 102 + (ghost_table[64, bg_col] >> 6)
            float ghost_row = (64.0 + 0.5) / 256.0;
            float ghost_col = bg_idx;
            float ghost_val = texture(u_fade, vec2(ghost_col, ghost_row)).r * 255.0;
            pal_idx = (102.0 + floor(ghost_val / 64.0)) / 255.0;
        }
        else // op == 3
        {
            // Blink+gems: ghost_table[row, bg_col] + 2
            float ghost_row = (float(r_val) + 0.5) / 256.0;
            float ghost_col = bg_idx;
            float ghost_val = texture(u_fade, vec2(ghost_col, ghost_row)).r * 255.0;
            pal_idx = (ghost_val + 2.0) / 255.0;
        }
    }

    vec4 pal = texture(u_palette, vec2(pal_idx, 0.5));
    fragColor = vec4(pal.rgb, 1.0);
}
)glsl";

// Zoom-box tile fragment shader.
// Draws individual dungeon tile quads from the R8 tile atlas for the
// ZBM_OVERHEAD zoom box, replacing the flat palette-color tiles with the
// actual top-face texture.  Index 0 is discarded (transparent pixels within
// tiles).  Blending must be enabled by the caller.
// Vertex shader: PALETTE_BLIT_VERTEX_SHADER (plain UV passthrough quad).
constexpr const char* ZOOM_TILE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in  vec2 v_uv;
out vec4 fragColor;
uniform sampler2DArray u_index;  // R8 tile atlas array (layer 0 = variation 0)
uniform sampler2D u_palette;  // RGBA8 — 256×1 game palette
uniform vec4  u_clip_rect;     // (x0, y0, x1, y1) in game screen pixels
uniform float u_clip_radius;   // corner radius; < 0 = no clip
uniform float u_clip_screen_h; // screen height for Y flip
void main()
{
    if (u_clip_radius >= 0.0)
    {
        vec2 p = vec2(gl_FragCoord.x, u_clip_screen_h - gl_FragCoord.y);
        vec2 center  = (u_clip_rect.xy + u_clip_rect.zw) * 0.5;
        vec2 halfSz  = (u_clip_rect.zw - u_clip_rect.xy) * 0.5;
        vec2 d = abs(p - center) - halfSz + u_clip_radius;
        float dist = length(max(d, 0.0)) - u_clip_radius;
        if (dist > 0.0) discard;
    }
    float idx = texture(u_index, vec3(v_uv, 0.0)).r;
    // Index 0 within tile data is a transparent/padding pixel — discard so the
    // parchment background or unrevealed fill shows through tile edges/borders.
    if (idx < (0.5 / 255.0)) discard;
    vec4 pal = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(pal.rgb, 1.0);
}
)glsl";

// Landview zoom fragment shader.
// Used by RendererOpenGL::SubmitLandviewZoom() for the campaign-map zoom
// transition (frontzoom_to_point).  Draws map_screen (GL_R8, 1280×960) as
// a fullscreen opaque quad, computing source UVs from gl_FragCoord so that
// the zoom centre in map space aligns with the zoom centre in screen space.
// Vertex shader: PALETTE_BLIT_VERTEX_SHADER (shared; v_uv unused here).
constexpr const char* LANDVIEW_ZOOM_FRAGMENT_SHADER = R"glsl(
#version 330 core
out vec4 fragColor;

uniform sampler2D u_index;       // R8  — map_screen palette indices
uniform sampler2D u_palette;     // RGBA8 — 256×1 game palette
uniform vec2  u_center_map;      // zoom centre in map texels (map_x, map_y)
uniform vec2  u_screen_center;   // zoom centre in screen pixels (game y-down)
uniform float u_scale;           // src_delta / 256.0  (source texels per screen pixel)
uniform vec2  u_inv_map_size;    // (1 / MAP_W, 1 / MAP_H)
uniform float u_screen_h;        // screen height in pixels

void main()
{
    // gl_FragCoord.xy is at pixel centre (x=0.5 at left column, y=0.5 at bottom row).
    // Convert from OpenGL convention (y increases upward) to game convention (y=0 top).
    vec2 fragXY = vec2(gl_FragCoord.x - 0.5, u_screen_h - gl_FragCoord.y - 0.5);

    // Map screen position to source texel (mirrors frontzoom_to_point arithmetic).
    vec2 srcXY = u_center_map + (fragXY - u_screen_center) * u_scale;

    // Normalise to [0,1] UV.  GL_CLAMP_TO_EDGE naturally replicates the edge
    // pixel for any fragment that maps outside the 1280×960 source image.
    vec2 uv = srcXY * u_inv_map_size;

    float idx = texture(u_index, uv).r;
    fragColor  = vec4(texture(u_palette, vec2(idx, 0.5)).rgb, 1.0);
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
// Unit 0 = sprite atlas (R8 GL_TEXTURE_2D).  Unit 1 = palette (sampler2D 256×1).
constexpr const char* UI_SPRITE_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;  // unit 0: R8 palette-index atlas
uniform sampler2D u_palette;       // unit 1: RGBA8 256×1 palette
out vec4 fragColor;
void main()
{
    float idx = texture(u_sprite_atlas, v_uv).r;
    if (idx < (0.5 / 255.0)) discard;
    vec4 pal = texture(u_palette, vec2(idx, 0.5));
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";

// Program 1b: truecolour atlas sprites (RGBA8).  Same role as UI_SPRITE but the
// atlas already holds materialised RGBA, so no palette lookup is needed.  Used
// when the sprite atlas is running in truecolour mode (palette_mode=TRUECOLOUR).
// Unit 0 = sprite atlas (RGBA8 GL_TEXTURE_2D).  Straight-alpha; index-0 texels
// were baked to alpha 0 at atlas build time.
constexpr const char* UI_SPRITE_RGBA_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;  // unit 0: RGBA8 materialised atlas
out vec4 fragColor;
void main()
{
    vec4 tex = texture(u_sprite_atlas, v_uv);
    if (tex.a < (0.5 / 255.0)) discard;
    fragColor = vec4(tex.rgb * v_color.rgb, tex.a * v_color.a);
}
)glsl";
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
// Unit 1 = palette (sampler2D 256×1, RGBA8).
// Unit 2 = fade table (R8 GL_TEXTURE_2D, 256 columns × 256 rows).
// uniform u_remap_row selects which row of the fade table to apply.
constexpr const char* UI_REMAP_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_sprite_atlas;  // unit 0: R8 palette-index atlas
uniform sampler2D u_palette;       // unit 1: RGBA8 256×1 palette
uniform sampler2D u_fade_table;    // unit 2: R8 256x256 remap LUT
uniform float u_remap_row;         // 0..255 — which row of the fade table
out vec4 fragColor;
void main()
{
    float idx_f = texture(u_sprite_atlas, v_uv).r;
    if (idx_f < (0.5 / 255.0)) discard;
    float remap_y = (u_remap_row + 0.5) / 256.0;
    float remapped_f = texture(u_fade_table, vec2(idx_f, remap_y)).r;
    vec4 pal = texture(u_palette, vec2(remapped_f, 0.5));
    fragColor = vec4(pal.rgb * v_color.rgb, v_color.a);
}
)glsl";

// ── FBO / PiP composite shader ────────────────────────────────────────────────
// Blits an RGBA8 FBO colour attachment directly to the screen quad.
// Used by GLUIRenderer::SubmitFBOQuad() to composite the picture-in-picture
// isometric viewport over the parchment map.  No palette lookup needed.
constexpr const char* UI_FBO_FRAGMENT_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_fbo_tex;   // unit 0: RGBA8 FBO colour texture
uniform vec4  u_clip_rect;     // (x0, y0, x1, y1) in game screen pixels
uniform float u_clip_radius;   // corner radius; < 0 = no clip
uniform float u_clip_screen_h; // screen height for Y flip
out vec4 fragColor;
void main()
{
    if (u_clip_radius >= 0.0)
    {
        vec2 p = vec2(gl_FragCoord.x, u_clip_screen_h - gl_FragCoord.y);
        vec2 center  = (u_clip_rect.xy + u_clip_rect.zw) * 0.5;
        vec2 halfSz  = (u_clip_rect.zw - u_clip_rect.xy) * 0.5;
        vec2 d = abs(p - center) - halfSz + u_clip_radius;
        float dist = length(max(d, 0.0)) - u_clip_radius;
        if (dist > 0.0) discard;
    }
    fragColor = texture(u_fbo_tex, v_uv);
}
)glsl";

// ── Map Fade Transition shaders ───────────────────────────────────────────────
// Full-screen wipe between the parchment overhead map and the 3D game view.
// u_parchment = tex unit 0  — decoded RGBA snapshot of the parchment view.
// u_world     = tex unit 1  — decoded RGBA snapshot of the 3D world view.
// u_step      = 0.0 (full parchment) → 32.0 (full 3D world) for fade-in;
//               reversed for fade-out.
//
// The warp math is a faithful GLSL port of map_fade() in engine_redraw.c:
//   • Both source images are UV-warped by functions of step and screen position
//     that create the original elastic "pinch from centre" distortion.
//   • Each image is multiplied by a fade factor (parchment dims, world brightens)
//     matching the original fade_tbl rows used in the CPU path.
//   • The two contributions are added and clamped, reproducing the ghost-table
//     additive-mix in palette space at full RGBA precision.
//
// UV convention:
//   v_uv from the vertex shader:  (0,0)=bottom-left, (1,1)=top-right (GL default).
//   CPU image rows:                row 0 = top of image → texture y = 1 after upload.
//   Warp computations use fy_cpu = 1-v_uv.y so (0)=top and (1)=bottom matches the
//   CPU loop direction; sample UVs flip back to GL convention before texture().
constexpr const char* MAP_FADE_VERT_SHADER = R"glsl(
#version 330 core
layout(location = 0) in vec2 a_pos;
out vec2 v_uv;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_pos * 0.5 + 0.5;
}
)glsl";

constexpr const char* MAP_FADE_FRAG_SHADER = R"glsl(
#version 330 core
in vec2 v_uv;
uniform sampler2D u_parchment; // unit 0 — parchment RGBA snapshot
uniform sampler2D u_world;     // unit 1 — 3D world RGBA snapshot
uniform float u_step;          // 0.0..32.0
out vec4 fragColor;

void main()
{
    float a6 = u_step;
    // Screen-space UV where y=0 is bottom, y=1 is top.
    float fx = v_uv.x;
    // Convert to CPU-image row fraction: 0=top row, 1=bottom row.
    float fy = 1.0 - v_uv.y;

    // Original xmax=320 is the scale base for the warp constants.
    const float xmax = 320.0;

    // ── Parchment (srcbuf1) warp — dims as step increases ──────────────────
    // Matches: xt[0] = clamp(4*(32-a6) + ix*(xmax-8*(32-a6))/xmax, 0, xmax)
    //          yt[0] as xmax row-offset scaled by ymax/xmax
    float wp = 32.0 - a6;
    float uv_px = clamp(fx + wp * (4.0 - 8.0 * fx) / xmax, 0.0, 1.0);
    float uv_py = clamp(fy + wp * 4.0 * (1.0 - 2.0 * fy) / xmax, 0.0, 1.0);

    // ── World (srcbuf2) warp — brightens as step increases ─────────────────
    float ww = a6;
    float uv_wx = clamp(fx + ww * (4.0 - 8.0 * fx) / xmax, 0.0, 1.0);
    float uv_wy = clamp(fy + ww * 4.0 * (1.0 - 2.0 * fy) / xmax, 0.0, 1.0);

    // Convert warp outputs back to GL texture convention (y flipped).
    float samp_py = 1.0 - uv_py;
    float samp_wy = 1.0 - uv_wy;

    // Fade factors matching fade_tbl rows: x0base=a6<<8 (parch), y0base=(32-a6)<<8 (world).
    float f_parch = wp  / 32.0;   // parchment: 1→0 ((32-a6)/32) — dims as step increases
    float f_world = ww  / 32.0;   // 3D world:  0→1 (a6/32)      — brightens as step increases

    vec3 c_parch = texture(u_parchment, vec2(uv_px, samp_py)).rgb * f_parch;
    vec3 c_world = texture(u_world,     vec2(uv_wx, samp_wy)).rgb * f_world;

    // Ghost-table equivalent: additive blend of two faded contributions.
    fragColor = vec4(clamp(c_parch + c_world, 0.0, 1.0), 1.0);
}
)glsl";