#version 450
// UI-текст: xyuv + rgba на вершину, ortho y-вниз из кода.
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aCol;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vCol;
uniform mat4 uVP;
void main() {
    vUV = aUV;
    vCol = aCol;
    gl_Position = uVP * vec4(aPos, 0.0, 1.0);
}
