#version 450
// Фулскрин-треугольник под SDF-реймарш. Камера — из UBO во фрагменте.
void main() {
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
}
