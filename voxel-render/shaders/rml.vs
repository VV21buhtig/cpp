#version 450 core
// Вершины RmlUi: позиция(px) + цвет(premultiplied) + uv. _transform уже включает ortho.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;
uniform vec2 _translate;
uniform mat4 _transform;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    gl_Position = _transform * vec4(inPosition + _translate, 0.0, 1.0);
}
