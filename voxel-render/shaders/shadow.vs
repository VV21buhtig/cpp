#version 450 core
// P2a теневая карта: только глубина от солнца. VAO чанков общий (aPos = location 0).
layout (location = 0) in vec3 aPos;
uniform mat4 lightSpace;
uniform mat4 model;
void main()
{
    gl_Position = lightSpace * model * vec4(aPos, 1.0);
}
