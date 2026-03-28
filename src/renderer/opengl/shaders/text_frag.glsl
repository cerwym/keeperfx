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