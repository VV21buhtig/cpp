#include "engine/struct/mine.h"
#include "engine/struct/place.h"
#include "engine/world.h"
#include <cstdlib>

void stampMines(World& w, const PlacementConfig& cfg) {
    int W = w.sizeX(), D = w.sizeZ();
    int rcx = (W + 15) / 16, rcz = (D + 15) / 16;
    int nrcx = (rcx + cfg.spacing - 1) / cfg.spacing;
    int nrcz = (rcz + cfg.spacing - 1) / cfg.spacing;
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    for (int rz = 0; rz < nrcz; rz++)
        for (int rx = 0; rx < nrcx; rx++) {
            int ox, oz;
            gridCandidate(w.seed_, cfg.salt, rx, rz, cfg.spacing, cfg.separation, &ox, &oz);
            int ccx = rx * cfg.spacing + ox, ccz = rz * cfg.spacing + oz;
            if (ccx < 0 || ccz < 0 || ccx >= rcx || ccz >= rcz) continue;
            if (cellRand(w.seed_, cfg.salt + 1, rx, rz) > cfg.chance) continue;
            int x = ccx * 16 + (int)(cellRand(w.seed_, cfg.salt + 2, rx, rz) * 16);
            int z = ccz * 16 + (int)(cellRand(w.seed_, cfg.salt + 3, rx, rz) * 16);
            int y = 4 + (int)(cellRand(w.seed_, cfg.salt + 4, rx, rz) * 7); // 4..10
            int dir = (int)(cellRand(w.seed_, cfg.salt + 5, rx, rz) * 4);
            for (int s = 0; s < 60; s++) {
                // камера 1x2: роем только opaque (воду/лаву не трогаем — не топим)
                if (World::isSolid(w.getBlock(x, y, z))) w.setBlock(x, y, z, 0);
                if (World::isSolid(w.getBlock(x, y + 1, z))) w.setBlock(x, y + 1, z, 0);
                // Комната 3x2x3 каждые 15 шагов: только solid (флюиды не вскрываем).
                if (s % 15 == 14) {
                    for (int ax = -1; ax <= 1; ax++)
                        for (int az = -1; az <= 1; az++)
                            for (int ay = 0; ay <= 1; ay++) {
                                unsigned char b = w.getBlock(x + ax, y + ay, z + az);
                                if (World::isSolid(b)) w.setBlock(x + ax, y + ay, z + az, 0);
                            }
                }
                float r = cellRand(w.seed_, cfg.salt + 100 + s, rx * 7 + s, rz * 13 - s);
                if (r < 0.25f) dir = (dir + 1) % 4;
                else if (r < 0.35f) dir = (dir + 3) % 4;
                else if (r < 0.42f) y += (cellRand(w.seed_, cfg.salt + 200 + s, rx, rz) < 0.5f) ? 1 : -1;
                x += dx[dir]; z += dz[dir];
                if (x < 1 || z < 1 || x >= W - 1 || z >= D - 1 || y < 2 || y > 14) break;
            }
        }
}
