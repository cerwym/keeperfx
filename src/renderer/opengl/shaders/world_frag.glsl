/* world_frag.glsl — World geometry fragment shader
 * Target: GLSL ES 2.0 (#version 100) — see world_vert.glsl for platform list.
 *
 * Pipeline:
 *   1. Sample u_tile_atlas at v_uv  → raw 8-bit palette index (stored in .r)
 *   2. Sample u_fade_table at (index, shade) → final lit RGBA colour
 *
 * This replaces the entire CPU inner loop in bflib_render_trig.c / gpoly.c:
 *   pixel = render_fade_tables[texture_map[uv] | (shade & 0xFF00)]
 * as a single 2D texture lookup per fragment.
 *
 * Textures:
 *   u_tile_atlas  — GL_R8 (or GL_LUMINANCE on ES 2.0): tile texels as palette
 *                   indices packed into the TileAtlas.  Sampled with NEAREST.
 *   u_fade_table  — GL_R8 (or GL_LUMINANCE on ES 2.0): 256×256 pre-computed
 *                   lighting table.  Row = palette index, column = shade level.
 *                   Sampled with NEAREST (no interpolation across shade steps).
 *   u_palette     — GL_RGBA8 (or GL_RGBA on ES 2.0): 256×1 expanded palette.
 *                   Converts the fade-table output index to final RGBA colour.
 */
#version 100

precision mediump float;

/* Interpolated from vertex shader */
varying vec2  v_uv;
varying float v_shade;

/* Samplers set by GLWorldViewRenderer::BeginWorldPass() */
uniform sampler2D u_tile_atlas;   /* R channel = 8-bit palette index [0,1] */
uniform sampler2D u_fade_table;   /* 256×256 lighting LUT, R = shaded index */
uniform sampler2D u_palette;      /* 256×1  RGBA expanded palette           */

void main()
{
    /* Step 1: fetch raw palette index from tile atlas (stored in red channel) */
    float raw_idx = texture2D(u_tile_atlas, v_uv).r;

    /* Step 2: look up the shaded (lit) palette index from the fade table.
     * fade_table[raw_idx * 255][v_shade * 255] → shaded_idx */
    float shaded_idx = texture2D(u_fade_table, vec2(raw_idx, v_shade)).r;

    /* Step 3: expand shaded palette index to RGBA via palette texture */
    gl_FragColor = texture2D(u_palette, vec2(shaded_idx, 0.5));
}
