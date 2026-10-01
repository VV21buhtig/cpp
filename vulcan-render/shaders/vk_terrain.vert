#version 450
// demo-3a pulling (Ch05-рецепт): vertex-input пустой, вершины лежат в SSBO
// плоским float-массивом (pos3+nrm3+uv2+tile+ao = 10), индекс = gl_VertexIndex.
// Ручные оффсеты вместо struct — vec3 в std430 занял бы 16 байт и развалил пак.
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 sunDir;
    vec4 sunCol;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fog;
    vec4 misc;
    vec4 viewPos;
} frame;

layout(set = 0, binding = 2) readonly buffer Quads { float vdata[]; } qb;

layout(push_constant) uniform Push { mat4 model; } pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vTile;
layout(location = 4) out float vAO;

void main() {
    uint b = uint(gl_VertexIndex) * 10u;
    vec3 p = vec3(qb.vdata[b], qb.vdata[b + 1u], qb.vdata[b + 2u]);
    vec3 n = vec3(qb.vdata[b + 3u], qb.vdata[b + 4u], qb.vdata[b + 5u]);
    vec4 w = pc.model * vec4(p, 1.0);
    vPos = w.xyz;
    vNrm = mat3(pc.model) * n; // model только переносы — нормали целы
    vUV = vec2(qb.vdata[b + 6u], qb.vdata[b + 7u]);
    vTile = qb.vdata[b + 8u];
    vAO = qb.vdata[b + 9u];
    gl_Position = frame.viewProj * w;
}
