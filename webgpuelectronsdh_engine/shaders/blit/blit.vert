#version 450
// Блит 2D-кадра: фулскрин без VBO, сэмпл RGBA-текстуры.
out vec2 UV;
void main() {
    vec2 v = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(v * 2.0 - 1.0, 0.0, 1.0);
    UV = v;
}
