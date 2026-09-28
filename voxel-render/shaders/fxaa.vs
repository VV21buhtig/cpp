#version 450 core
// Фулскрин-треугольник без VBO (как sky.vs) + uv 0..1 по экрану.
out vec2 vUV;
void main() {
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = v;
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}
