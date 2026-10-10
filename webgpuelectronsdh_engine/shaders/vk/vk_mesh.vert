#version 450
// Кадр: shared struct Frame (std140) — зеркало VkFrame в csrc/core/vk_core.c.
// Формулы — их lighting (V), те же константы что в GL-ядре.
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 sunDir;    // xyz, w=shadowOn (0)
    vec4 ambSky;    // rgb
    vec4 ambGnd;    // rgb
    vec4 fogColor;  // rgb, w=gamma
    vec4 fogSat;    // near, far, sat, time
    vec4 viewPos;
    vec4 sunDiff;   // rgb
    vec4 sunSpec;   // rgb, w=shininess
    vec4 sdfMin;    // xyz мировой угол SDF-объёма (176x64x176 тороид)
} fr;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aTile;
layout(location = 4) in float aAO;
// day/night (5,6) мертвы как у их lighting.fs — не объявляем, страйд общий.

layout(push_constant) uniform Push { mat4 model; } pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vTile;
layout(location = 4) out float vAO;

void main() {
    vec4 w = pc.model * vec4(aPos, 1.0);
    vPos = w.xyz;
    vNrm = aNrm; // model только переносы
    vUV = aUV;
    vTile = aTile;
    vAO = aAO;
    gl_Position = fr.viewProj * w;
}
