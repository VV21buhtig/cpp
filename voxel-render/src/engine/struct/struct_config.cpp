#include "engine/struct/config.h"
#include <cstdio>
#include <cstring>

void StructConfigs::load(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return; // нет файла — дефолты
    char line[256];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[64];
        int sp, se, sa, tb = 4, tr = 2;
        float ch, hc = 0.6f;
        int got = sscanf(line, "%63s %d %d %d %f %d %d %f", name, &sp, &se, &sa, &ch, &tb, &tr, &hc);
        if (got < 5) continue;
        if (sp < 1) sp = 1;
        if (se < 0) se = 0;
        if (se >= sp) se = sp - 1;
        if (ch < 0) ch = 0;
        if (ch > 1) ch = 1;
        if (!strcmp(name, "tree")) {
            tree.spacing = sp; tree.separation = se; tree.salt = sa; tree.chance = ch;
            if (got >= 8) {
                if (tb < 1) tb = 1; if (tb > 12) tb = 12;
                if (tr < 0) tr = 0; if (tr > 12) tr = 12;
                if (hc < 0) hc = 0; if (hc > 1) hc = 1;
                tree.trunkBase = tb; tree.trunkRand = tr; tree.holeChance = hc;
            }
        }
        else if (!strcmp(name, "mine")) mine = PlacementConfig{sp, se, sa, ch};
        n++;
    }
    fclose(f);
    printf("structures.cfg: %d entries\n", n);
}
