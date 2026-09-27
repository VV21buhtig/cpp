#include "engine/struct/tree.h"
#include "engine/struct/config.h"
#include <cstdlib>
#include "engine/struct/place.h"
#include "engine/world.h"

void stampTrees(World& w, const TreeConfig& cfg) {
    int W = w.sizeX(), D = w.sizeZ();
    int rcx = (W + 15) / 16, rcz = (D + 15) / 16; // регионов по сетке чанков
    int nrcx = (rcx + cfg.spacing - 1) / cfg.spacing;
    int nrcz = (rcz + cfg.spacing - 1) / cfg.spacing;
    for (int rz = 0; rz < nrcz; rz++)
        for (int rx = 0; rx < nrcx; rx++) {
            int ox, oz;
            gridCandidate(w.seed_, cfg.salt, rx, rz, cfg.spacing, cfg.separation, &ox, &oz);
            int ccx = rx * cfg.spacing + ox, ccz = rz * cfg.spacing + oz;
            if (ccx < 0 || ccz < 0 || ccx >= rcx || ccz >= rcz) continue;
            // шанс + джиттер внутри чанка-кандидата
            float roll = cellRand(w.seed_, cfg.salt + 1, rx, rz);
            if (roll > cfg.chance) continue;
            int tx = ccx * 16 + (int)(cellRand(w.seed_, cfg.salt + 2, rx, rz) * 16);
            int tz = ccz * 16 + (int)(cellRand(w.seed_, cfg.salt + 3, rx, rz) * 16);
            if (tx < 2 || tz < 2 || tx >= W - 2 || tz >= D - 2) continue;
            // земля: верх solid должен быть травой
            int top = -1;
            for (int y = Chunk::SY - 1; y >= 0; y--)
                if (World::isSolid(w.getBlock(tx, y, tz))) { top = y; break; }
            if (top < 0 || w.getBlock(tx, top, tz) != B_GRASS) continue;
            int th = cfg.trunkBase + (int)(cellRand(w.seed_, cfg.salt + 4, rx, rz) * (cfg.trunkRand + 1));
            if (top + th + 2 >= Chunk::SY) continue;
            // просвет под крону: колонна должна быть воздухом
            bool clear = true;
            for (int y = top + 1; y <= top + th + 1 && clear; y++)
                if (w.getBlock(tx, y, tz) != 0) clear = false;
            if (!clear) continue;
            // ствол
            for (int y = top + 1; y <= top + th; y++) w.setBlock(tx, y, tz, B_LOG);
            // крона: 2 слоя r2 с выкушенными углами + 3x3 + крест
            for (int ly = 0; ly < 2; ly++) {
                int y = top + th - 1 + ly;
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++) {
                        if (abs(dx) == 2 && abs(dz) == 2 &&
                            cellRand(w.seed_, cfg.salt + 10 + ly, tx + dx * 3 + dz, tz + dz * 5 - dx) < cfg.holeChance)
                            continue; // угол выкушен
                        if (w.getBlock(tx + dx, y, tz + dz) == 0) w.setBlock(tx + dx, y, tz + dz, B_LEAVES);
                    }
            }
            for (int dx = -1; dx <= 1; dx++)
                for (int dz = -1; dz <= 1; dz++)
                    if (w.getBlock(tx + dx, top + th + 1, tz + dz) == 0)
                        w.setBlock(tx + dx, top + th + 1, tz + dz, B_LEAVES);
            const int cr[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto& o : cr)
                if (w.getBlock(tx + o[0], top + th + 2, tz + o[1]) == 0)
                    w.setBlock(tx + o[0], top + th + 2, tz + o[1], B_LEAVES);
            if (w.getBlock(tx, top + th + 2, tz) == 0) w.setBlock(tx, top + th + 2, tz, B_LEAVES);
        }
}
