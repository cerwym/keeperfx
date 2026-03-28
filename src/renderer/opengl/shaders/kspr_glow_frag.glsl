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
