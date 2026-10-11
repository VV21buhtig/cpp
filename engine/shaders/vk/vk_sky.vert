#version 450
// Фулскрин-треугольник без VBO. Push 112: invVP + палитра (res в w цветов).
layout(push_constant) uniform Push {
    mat4 invVP;
    vec4 sunDirNight; // xyz, w=nightF
    vec4 topColor;    // rgb, w=resH
    vec4 horColor;    // rgb, w=resW
} pc;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 v = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}
