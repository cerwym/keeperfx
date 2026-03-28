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
