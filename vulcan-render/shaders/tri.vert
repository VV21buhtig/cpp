#version 450
// Фулскрин-треугольник без VBO: вершины из gl_VertexIndex (Vulkan-имя, в GL это gl_VertexID).
void main() {
    vec2 v = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}
