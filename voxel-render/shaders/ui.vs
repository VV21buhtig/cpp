#version 450 core
layout (location = 0) in vec3 aPos;  // пиксели, y вниз
layout (location = 1) in vec4 aCol;
uniform vec2 res;
out vec4 vCol;
void main() {
    vec2 ndc = vec2(aPos.x / res.x * 2.0 - 1.0, 1.0 - aPos.y / res.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vCol = aCol;
}
