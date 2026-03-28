#version 330 core
in vec2  v_uv;
in float v_shade;
uniform sampler2D u_tile_atlas;
out vec4 fragColor;
void main()
{
    // Atlas is RGBA8 pre-decoded from the palette; multiply RGB by shade directly.
    vec4 col = texture(u_tile_atlas, v_uv);
    fragColor = vec4(col.rgb * v_shade, 1.0);
}
