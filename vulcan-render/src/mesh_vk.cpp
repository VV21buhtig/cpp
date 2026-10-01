// mesh_vk: поквадратный мешер (см. mesh_vk.h). 1 грань = 1 запись u32
// (биты: x4+z4+y6+face3+tile6+ao8+flip1). Топологию (углы, winding, flip)
// разворачивает ВЕРШИННЫЙ шейдер статическими таблицами (Ch05+K-рецепт):
// индекс-буфер не нужен, draw instanced (6 вершин x N квадов).
#include "mesh_vk.h"
#include "engine/world.h"
#include "engine/blocks.h"
#include <cstring>

std::vector<uint32_t> buildChunkVK(const World& w, int cx, int cz) {
    std::vector<uint32_t> out;
    out.reserve(8192);
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
                    // flip по AO (0fps-оригинал). Топологию разворачивает VS.
                    bool flip = (ca[0] + ca[2] > ca[3] + ca[1]);
                    // Пак: x4+z4+y6 (локальные!) + face3 + tile6 + ao8 + flip1 = 32 бита.
                    uint32_t rec = (uint32_t)(x & 15) | ((uint32_t)(z & 15) << 4) |
                                   ((uint32_t)(y & 63) << 8) |
                                   ((uint32_t)(ax * 2 + (sn > 0 ? 0 : 1)) << 14) |
                                   (((uint32_t)tile & 63) << 17) |
                                   (((uint32_t)ca[0] | ((uint32_t)ca[1] << 2) |
                                     ((uint32_t)ca[2] << 4) | ((uint32_t)ca[3] << 6)) << 23) |
                                   ((uint32_t)(flip ? 1 : 0) << 31);
                    out.push_back(rec);
                }
            }
    return out;
}
