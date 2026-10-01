#version 450 core
// P2i debug: фулскрин-треугольник с UV для просмотра теневой карты.
void main() {
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}
