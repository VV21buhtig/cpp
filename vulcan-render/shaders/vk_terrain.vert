#version 450
// demo-3b: 1 запись u32/грань -> 4 угла в VS (Ch05+K-рецепт). Индекс-буфера нет:
// draw instanced (6 вершин x N), угол из статических таблиц + flip-бит.
// Биты: x4+z4+y6 (локальные!) + face3 + tile6 + ao8 + flip1. Мир — через model.
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

layout(set = 0, binding = 2) readonly buffer Quads { uint qd[]; } qb;
// demo-3c: чанк — из видимого списка compute (gl_DrawID = слот), origin из meta.
struct ChunkMeta { uint quadOff; uint quadCount; vec2 origin; };
layout(set = 0, binding = 3) readonly buffer MetaB { ChunkMeta metas[]; };
layout(set = 0, binding = 4) readonly buffer VisB { uint vis[]; };

layout(push_constant) uniform Push { mat4 model; } pc;

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vTile;
layout(location = 4) out float vAO;

// Углы квада (те же c[4] что в мешере): 0:(0,0) 1:(1,0) 2:(1,1) 3:(0,1).
// Таблицы обхода — дословно winding из GL-теста: [case][flip][6],
// case: осьZ? (знак+?0:1) : (знак+?2:3).
const int DU[4] = int[4](0, 1, 1, 0);
const int DV[4] = int[4](0, 0, 1, 1);
const int TRI[48] = int[48](
    0,1,2, 0,2,3,  1,2,3, 1,3,0,   // z+
    0,2,1, 0,3,2,  1,3,2, 1,0,3,   // z-
    0,2,1, 0,3,2,  1,3,2, 1,0,3,   // x+/y+
    0,1,2, 0,2,3,  1,2,3, 1,3,0);  // x-/y-

void main() {
    // Чанк закодирован в старших битах firstInstance (см. cull.comp QUADBIAS):
    // gl_InstanceIndex = firstInstance + i. gl_DrawID не используем (нет в glslang).
    uint chunk = uint(gl_InstanceIndex) / 1048576u;
    uint quad = uint(gl_InstanceIndex) - chunk * 1048576u;
    uint rec = qb.qd[quad];
    uint lx = rec & 15u, lz = (rec >> 4) & 15u, ly = (rec >> 8) & 63u;
    uint f = (rec >> 14) & 7u;
    int ax = int(f >> 1);
    int sn = ((f & 1u) == 0u) ? 1 : -1;
    uint tile = (rec >> 17) & 63u;
    uint ao4 = (rec >> 23) & 255u;
    uint flip = (rec >> 31) & 1u;

    int cs = (ax == 2) ? (sn > 0 ? 0 : 1) : (sn > 0 ? 2 : 3);
    int ci = TRI[(cs * 2 + int(flip)) * 6 + gl_VertexIndex];
    int du = DU[ci], dv = DV[ci];

    ivec3 A = (ax == 0) ? ivec3(0,0,1) : ivec3(1,0,0);
    ivec3 B = (ax == 0) ? ivec3(0,1,0) : ((ax == 1) ? ivec3(0,0,1) : ivec3(0,1,0));
    ivec3 n = ivec3(0, 0, 0);
    n[ax] = sn;
    ivec3 base = ivec3(int(lx), int(ly), int(lz))
               + (sn > 0 ? n : ivec3(0, 0, 0)) + du * A + dv * B;

    // Мир: origin чанка из meta (id чанка — из firstInstance, vis[] не нужен).
    vec2 org = metas[chunk].origin;
    vec4 w = vec4(vec3(base) + vec3(org.x, 0.0, org.y), 1.0);
    vPos = w.xyz;
    vNrm = vec3(n); // model только переносы
    // UV мировые как в GL (ось0: z/y; ось1: x/z; ось2: x/y)
    if (ax == 0) vUV = vec2(w.z, w.y);
    else if (ax == 1) vUV = vec2(w.x, w.z);
    else vUV = vec2(w.x, w.y);
    vTile = float(tile);
    vAO = float((ao4 >> uint(2 * ci)) & 3u);
    gl_Position = frame.viewProj * w;
}
