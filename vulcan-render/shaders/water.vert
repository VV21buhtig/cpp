#version 450
// demo-5w вода: та же u32-идея, формат свой (flow вместо tile/flip).
// x4+z4+y6+face3+flow4+ao8. Верх грани — на уровне flow/8, flip фиксирован.
layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 invViewProj;
    vec4 sunDir;
    vec4 sunCol;
    vec4 ambSky;
    vec4 ambGnd;
    vec4 fog;
    vec4 misc;
    vec4 viewPos;
} frame;

layout(set = 0, binding = 7) readonly buffer WaterQuads { uint qd[]; } qb;
struct ChunkMeta { uint quadOff; uint quadCount; vec2 origin; };
layout(set = 0, binding = 8) readonly buffer WaterMetaB { ChunkMeta metas[]; };

layout(location = 0) out vec3 vPos;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vAO;

const int DU[4] = int[4](0, 1, 1, 0);
const int DV[4] = int[4](0, 0, 1, 1);
// Фикс-диагонали (noflip-варианты террейна): z+,z-,x+/y+,x-/y-.
const int TRW[24] = int[24](
    0,1,2, 0,2,3,
    0,2,1, 0,3,2,
    0,2,1, 0,3,2,
    0,1,2, 0,2,3);

void main() {
    uint rec = qb.qd[gl_InstanceIndex];
    uint lx = rec & 15u, lz = (rec >> 4) & 15u, ly = (rec >> 8) & 63u;
    uint f = (rec >> 14) & 7u;
    int ax = int(f >> 1);
    int sn = ((f & 1u) == 0u) ? 1 : -1;
    float lvl = float((rec >> 17) & 15u) / 8.0;
    uint ao4 = (rec >> 21) & 255u;
    uint chunk = uint(gl_InstanceIndex) / 1048576u;

    int cs = (ax == 2) ? (sn > 0 ? 0 : 1) : (sn > 0 ? 2 : 3);
    int ci = TRW[cs * 6 + gl_VertexIndex];
    int du = DU[ci], dv = DV[ci];
    ivec3 A = (ax == 0) ? ivec3(0,0,1) : ivec3(1,0,0);
    ivec3 B = (ax == 0) ? ivec3(0,1,0) : ((ax == 1) ? ivec3(0,0,1) : ivec3(0,1,0));
    ivec3 n = ivec3(0, 0, 0);
    n[ax] = sn;
    // Плоскость грани (без Y: высоту считаем отдельно через уровень).
    ivec3 bp = ivec3(int(lx), int(ly), int(lz))
             + (sn > 0 ? ivec3(n.x, 0, n.z) : ivec3(0, 0, 0)) + du * A + dv * B;
    float py;
    if (ax == 1 && sn > 0) py = float(ly) + lvl;        // верх воды
    else if (ax != 1 && dv == 1) py = float(ly) + lvl;  // верх боковины
    else py = float(bp.y);                              // дно блока
    vec2 org = metas[chunk].origin;
    vec4 w = vec4(vec3(float(bp.x), py, float(bp.z)) + vec3(org.x, 0.0, org.y), 1.0);
    vPos = w.xyz;
    vNrm = vec3(n);
    if (ax == 0) vUV = vec2(w.z, w.y);
    else if (ax == 1) vUV = vec2(w.x, w.z);
    else vUV = vec2(w.x, w.y);
    vAO = float((ao4 >> uint(2 * ci)) & 3u);
    gl_Position = frame.viewProj * w;
}
