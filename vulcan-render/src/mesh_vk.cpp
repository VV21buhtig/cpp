// mesh_vk: поквадратный мешер (см. mesh_vk.h). Winding-таблицы — дословно из
// GL-мешера (проверены winding-тестом), flip диагонали по AO (0fps-оригинал).
// Флуда нет и не будет (решение). Выход: 10 floats/вершина (pos3+nrm3+uv2+tile+ao),
// 6 вертексов на грань (2 триса, без индекса).
#include "mesh_vk.h"
#include "engine/world.h"
#include "engine/blocks.h"
#include <cstring>

std::vector<float> buildChunkVK(const World& w, int cx, int cz) {
    std::vector<float> out;
    out.reserve(4096 * 10);
    auto pushV = [&](float x, float y, float z, float nx, float ny, float nz,
                     float u, float v, float tile, float ao) {
        out.push_back(x); out.push_back(y); out.push_back(z);
        out.push_back(nx); out.push_back(ny); out.push_back(nz);
        out.push_back(u); out.push_back(v);
        out.push_back(tile); out.push_back(ao);
    };
    // направления: ось + знак. A/B — касательные (конвенция GL-мешера).
    const int AX[6] = {0, 0, 1, 1, 2, 2};
    const int SN[6] = {1, -1, 1, -1, 1, -1};
    const int AA[3][3] = {{0,0,1}, {1,0,0}, {1,0,0}}; // A-вектор по оси
    const int BB[3][3] = {{0,1,0}, {0,0,1}, {0,1,0}}; // B-вектор по оси
    for (int x = 0; x < 16; x++)
        for (int z = 0; z < 16; z++)
            for (int y = 0; y < 64; y++) {
                int wx = cx * 16 + x, wz = cz * 16 + z;
                unsigned char id = w.getBlock(wx, y, wz);
                if (!id || !w.isSolid(id)) continue; // флюиды/воздух — позже
                const BlockDef& dd = gBlocks.get(id);
                for (int f = 0; f < 6; f++) {
                    int ax = AX[f], sn = SN[f];
                    int n[3] = {0, 0, 0};
                    n[ax] = sn;
                    int ob = w.getBlock(wx + n[0], y + n[1], wz + n[2]);
                    const BlockDef& od = gBlocks.get(ob);
                    bool oOpaque = od.solid && !(od.cutout && ob != id);
                    if (oOpaque) continue;
                    int A[3] = {AA[ax][0], AA[ax][1], AA[ax][2]};
                    int B[3] = {BB[ax][0], BB[ax][1], BB[ax][2]};
                    float tile = (ax == 1) ? (float)(sn > 0 ? dd.tileTop : dd.tileBottom)
                                           : (float)dd.tileSide;
                    // Углы: AO по 0fps-пробам (оставляем — это не флуд).
                    // Флуда day/night НЕТ (решение: пародия на тень удалена из движка
                    // навсегда; солнце — за GPU/shadowmap demo-4, окклюзия — за RT AO).
                    float ca[4];
                    const int DUs[4] = {0, 1, 1, 0}, DVs[4] = {0, 0, 1, 1};
                    int o = (sn > 0) ? 0 : -1;
                    for (int k = 0; k < 4; k++) {
                        int du = DUs[k], dv = DVs[k];
                        int a0 = (du == 0) ? -1 : 0, b0 = (dv == 0) ? -1 : 0;
                        // G — клетка снаружи грани у угла
                        int G[3] = {wx + (sn > 0 ? n[0] : 0) + du * A[0] + dv * B[0],
                                    y + (sn > 0 ? n[1] : 0) + du * A[1] + dv * B[1],
                                    wz + (sn > 0 ? n[2] : 0) + du * A[2] + dv * B[2]};
                        auto at = [&](int q[3]) { return w.getBlock(q[0], q[1], q[2]); };
                        int c1[3] = {G[0] + n[0]*o + A[0]*a0, G[1] + n[1]*o + A[1]*a0, G[2] + n[2]*o + A[2]*a0};
                        int c2[3] = {G[0] + n[0]*o + B[0]*b0, G[1] + n[1]*o + B[1]*b0, G[2] + n[2]*o + B[2]*b0};
                        int cc[3] = {G[0] + n[0]*o + A[0]*a0 + B[0]*b0,
                                     G[1] + n[1]*o + A[1]*a0 + B[1]*b0,
                                     G[2] + n[2]*o + A[2]*a0 + B[2]*b0};
                        int s1 = at(c1) ? 1 : 0, s2 = at(c2) ? 1 : 0, ccm = at(cc) ? 1 : 0;
                        ca[k] = (ax == 1) ? 3.0f : ((s1 && s2) ? 0.0f : (float)(3 - (s1 + s2 + ccm)));
                    }
                    // flip по AO (0fps-оригинал).
                    bool flip = (ca[0] + ca[2] > ca[3] + ca[1]);
                    int tri[6];
                    if (ax == 2) {
                        if (sn > 0) { if (!flip) { int t[6]={0,1,2, 0,2,3}; memcpy(tri,t,sizeof t); } else { int t[6]={1,2,3, 1,3,0}; memcpy(tri,t,sizeof t); } }
                        else        { if (!flip) { int t[6]={0,2,1, 0,3,2}; memcpy(tri,t,sizeof t); } else { int t[6]={1,3,2, 1,0,3}; memcpy(tri,t,sizeof t); } }
                    } else {
                        if (sn > 0) { if (!flip) { int t[6]={0,2,1, 0,3,2}; memcpy(tri,t,sizeof t); } else { int t[6]={1,3,2, 1,0,3}; memcpy(tri,t,sizeof t); } }
                        else        { if (!flip) { int t[6]={0,1,2, 0,2,3}; memcpy(tri,t,sizeof t); } else { int t[6]={1,2,3, 1,3,0}; memcpy(tri,t,sizeof t); } }
                    }
                    for (int k = 0; k < 6; k++) {
                        int c = tri[k];
                        int du = DUs[c], dv = DVs[c];
                        float px = (float)(wx + (sn > 0 ? n[0] : 0) + du * A[0] + dv * B[0]);
                        float py = (float)(y + (sn > 0 ? n[1] : 0) + du * A[1] + dv * B[1]);
                        float pz = (float)(wz + (sn > 0 ? n[2] : 0) + du * A[2] + dv * B[2]);
                        // UV мировые как в GL (ось0: z/y; ось1: x/z; ось2: x/y)
                        float uu, vv;
                        if (ax == 0)      { uu = pz; vv = py; }
                        else if (ax == 1) { uu = px; vv = pz; }
                        else              { uu = px; vv = py; }
                        pushV(px, py, pz, (float)n[0], (float)n[1], (float)n[2],
                              uu, vv, tile, ca[c]);
                    }
                }
            }
    return out;
}
