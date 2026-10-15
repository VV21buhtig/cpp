#version 450
// UI-текст: R8-атлас как alpha, цвет из вершины.
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 outColor;
uniform sampler2D uTex;
void main() {
    float a = texture(uTex, vUV).r;
    outColor = vec4(vCol.rgb, vCol.a * a);
}
