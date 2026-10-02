#version 450
// demo-4 shadow depth: только позиция (глубина пишется сама).
// Квад — та же u32-запись, origin чанка из meta (как террейн-VS).
layout(set = 0, binding = 2) readonly buffer Quads { uint qd[]; } qb;
struct ChunkMeta { uint quadOff; uint quadCount; vec2 origin; };
layout(set = 0, binding = 3) readonly buffer MetaB { ChunkMeta metas[]; };
// Биндинги 2/3 — те же что в террейне (layout общий!).

layout(push_constant) uniform Push { mat4 lightSpace; } pc;

const int DU[4] = int[4](0, 1, 1, 0);
const int DV[4] = int[4](0, 0, 1, 1);
const int SQI[6] = int[6](0, 1, 2, 0, 2, 3);

void main() {
    uint rec = qb.qd[gl_InstanceIndex];
    uint lx = rec & 15u, lz = (rec >> 4) & 15u, ly = (rec >> 8) & 63u;
    uint f = (rec >> 14) & 7u;
    int ax = int(f >> 1);
    int sn = ((f & 1u) == 0u) ? 1 : -1;
    // Угол без flip не важен для глубины? ВАЖЕН (трещины) — но индекс идёт
    // напрямую 0..5: глубина та же при любой диагонали (плоский квад). OK.
    int ci = SQI[gl_VertexIndex];
    int du = DU[ci], dv = DV[ci];
    ivec3 A = (ax == 0) ? ivec3(0,0,1) : ivec3(1,0,0);
    ivec3 B = (ax == 0) ? ivec3(0,1,0) : ((ax == 1) ? ivec3(0,0,1) : ivec3(0,1,0));
    ivec3 n = ivec3(0, 0, 0);
    n[ax] = sn;
    ivec3 base = ivec3(int(lx), int(ly), int(lz))
               + (sn > 0 ? n : ivec3(0, 0, 0)) + du * A + dv * B;
    uint chunk = uint(gl_InstanceIndex) / 1048576u;
    vec2 org = metas[chunk].origin;
    gl_Position = pc.lightSpace * vec4(vec3(base) + vec3(org.x, 0.0, org.y), 1.0);
}
