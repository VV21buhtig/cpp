#include "engine/struct/rock.h"
#include "engine/struct/place.h"
#include "engine/world.h"

void stampRocks(World& w, const PlacementConfig& cfg) {
    int W = w.sizeX(), D = w.sizeZ();
    int rcx = (W + 15) / 16, rcz = (D + 15) / 16;
    int nrcx = (rcx + cfg.spacing - 1) / cfg.spacing;
    int nrcz = (rcz + cfg.spacing - 1) / cfg.spacing;
    const int SEA = 20;
    for (int rz = 0; rz < nrcz; rz++)
        for (int rx = 0; rx < nrcx; rx++) {
            int ox, oz;
            gridCandidate(w.seed_, cfg.salt, rx, rz, cfg.spacing, cfg.separation, &ox, &oz);
            int ccx = rx * cfg.spacing + ox, ccz = rz * cfg.spacing + oz;
            if (ccx < 0 || ccz < 0 || ccx >= rcx || ccz >= rcz) continue;
            if (cellRand(w.seed_, cfg.salt + 1, rx, rz) > cfg.chance) continue;
            int tx0 = ccx * 16 + (int)(cellRand(w.seed_, cfg.salt + 2, rx, rz) * 16);
            int tz0 = ccz * 16 + (int)(cellRand(w.seed_, cfg.salt + 3, rx, rz) * 16);
            // Джиттер по колонкам: клетка часто в океане — пробуем соседей в том же
            // чанке, выигрывает первая годная (детерминировано тем же хэшем).
            const int JO[5][2] = {{0,0},{8,0},{-8,0},{0,8},{0,-8}};
            for (int t = 0; t < 5; t++) {
                int tx = tx0 + JO[t][0], tz = tz0 + JO[t][1];
                if (tx < 1 || tz < 1 || tx >= W - 1 || tz >= D - 1) continue;
                int top = -1;
                for (int y = Chunk::SY - 1; y >= 0; y--)
                    if (World::isSolid(w.getBlock(tx, y, tz))) { top = y; break; }
                if (top < SEA - 1 || top + 2 >= Chunk::SY) continue; // глубь не трогаем
                unsigned char g = w.getBlock(tx, top, tz);
                if (g == B_LOG || g == B_LEAVES) continue; // не в крону
                // Основание 2 рядом + 1 сверху, только в воздух (полузакопан).
                int dx = (cellRand(w.seed_, cfg.salt + 4, rx, rz) < 0.5f) ? 1 : 0;
                int dz = dx ? 0 : 1;
                if (w.getBlock(tx, top + 1, tz) == 0) w.setBlock(tx, top + 1, tz, B_STONE);
                if (w.getBlock(tx + dx, top + 1, tz + dz) == 0)
                    w.setBlock(tx + dx, top + 1, tz + dz, B_STONE);
                if (w.getBlock(tx, top + 2, tz) == 0) w.setBlock(tx, top + 2, tz, B_STONE);
                break; // клетка занята — следующая
            }
        }
}
