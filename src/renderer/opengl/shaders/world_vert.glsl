/* world_vert.glsl — World geometry vertex shader
 * Target: GLSL ES 2.0 (#version 100) — compatible with:
 *   PC OpenGL 3.3 core (desktop GL backward-compat mode)
 *   Nintendo Switch (Mesa OpenGL ES 3.1, superset)
 *   PlayStation Vita (vitaGL OpenGL ES 2.0)
 *   3DS: transcribe to PICA200 assembly — same vertex data layout
 */
#version 100

/* Per-vertex inputs from WorldVertex struct:
 *   location 0: a_pos   — NDC position (x, y)
 *   location 1: a_uv    — tile atlas UV (u, v)
 *   location 2: a_shade — lighting intensity [0, 1]
 */
attribute vec2  a_pos;
attribute vec2  a_uv;
attribute float a_shade;

/* Passed to fragment shader for interpolation across the triangle */
varying vec2  v_uv;
varying float v_shade;

void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv        = a_uv;
    v_shade     = a_shade;
}
