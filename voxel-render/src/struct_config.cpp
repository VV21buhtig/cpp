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
        int sp, se, sa;
        float ch;
        if (sscanf(line, "%63s %d %d %d %f", name, &sp, &se, &sa, &ch) != 5) continue;
        if (sp < 1) sp = 1;
        if (se < 0) se = 0;
        if (se >= sp) se = sp - 1;
        if (ch < 0) ch = 0;
        if (ch > 1) ch = 1;
        PlacementConfig pc{sp, se, sa, ch};
        if (!strcmp(name, "tree")) tree = pc;
        else if (!strcmp(name, "mine")) mine = pc;
        n++;
    }
    fclose(f);
    printf("structures.cfg: %d entries\n", n);
}
