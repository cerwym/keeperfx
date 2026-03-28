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