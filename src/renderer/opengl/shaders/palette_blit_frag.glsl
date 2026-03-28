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
