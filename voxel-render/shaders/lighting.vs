#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in float aTile;
layout (location = 4) in float aAO;
layout (location = 5) in float aDay;   // P1c baked: солнце 0..15
layout (location = 6) in float aNight; // P1c baked: блоки 0..14

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out float Tile;
out float AO;
out float Day;
out float Night;
out vec4 ShadowPos; // P2a координаты в карте теней

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpace; // P2a матрица солнца для теней

void main()
{
    FragPos   = vec3(model * vec4(aPos, 1.0));
    Normal    = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTexCoords;
    Tile      = aTile;
    AO        = aAO;
    Day       = aDay;
    Night     = aNight;
    // normal-offset от acne (Luanti-идея: bias в вершинном по нормали)
    ShadowPos = lightSpace * model * vec4(aPos + aNormal * 0.03, 1.0);

    gl_Position = projection * view * vec4(FragPos, 1.0);
}