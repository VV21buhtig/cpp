#include "engine/save.h"
#include "engine/world.h"
#include <cstdio>

bool saveWorld(const World& w, const char* path) {
    // Атомарно через .tmp. VXW4: dims + seed + блоки + уровни флюидов.
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    const char magic[4] = {'V', 'X', 'W', '4'};
    bool ok = true;
    ok &= fwrite(magic, 1, 4, f) == 4;
    int dims[5] = {w.ncx(), w.ncz(), Chunk::SX, Chunk::SY, Chunk::SZ};
    ok &= fwrite(dims, sizeof(int), 5, f) == 5;
    ok &= fwrite(&w.seed_, sizeof(int), 1, f) == 1;
    for (int cz = 0; cz < w.ncz() && ok; cz++)
        for (int cx = 0; cx < w.ncx() && ok; cx++)
            ok &= fwrite(w.at(cx, cz).blocks, 1, sizeof(w.at(cx, cz).blocks), f) == sizeof(w.at(cx, cz).blocks);
    for (int cz = 0; cz < w.ncz() && ok; cz++)
        for (int cx = 0; cx < w.ncx() && ok; cx++)
            ok &= fwrite(w.at(cx, cz).flow, 1, sizeof(w.at(cx, cz).flow), f) == sizeof(w.at(cx, cz).flow);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}

bool loadWorld(World& w, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || magic[0] != 'V' || magic[1] != 'X' || magic[2] != 'W') {
        fclose(f);
        return false;
    }
    int dims[5];
    if (fread(dims, sizeof(int), 5, f) != 5 ||
        dims[2] != Chunk::SX || dims[3] != Chunk::SY || dims[4] != Chunk::SZ ||
        dims[0] <= 0 || dims[0] > 64 || dims[1] <= 0 || dims[1] > 64) {
        fclose(f);
        return false;
    }
    int seed = 1337;
    if (magic[3] == '4' || magic[3] == '3') {
        if (fread(&seed, sizeof(int), 1, f) != 1) { fclose(f); return false; }
    } else if (magic[3] != '2') { fclose(f); return false; }
    // VXW2 без сида: размер обязан совпасть с текущим миром
    if (magic[3] == '2' && (dims[0] != w.ncx() || dims[1] != w.ncz())) { fclose(f); return false; }
    w.init(dims[0], dims[1], seed);
    for (int cz = 0; cz < w.ncz(); cz++)
        for (int cx = 0; cx < w.ncx(); cx++)
            if (fread(w.at(cx, cz).blocks, 1, sizeof(w.at(cx, cz).blocks), f) != sizeof(w.at(cx, cz).blocks)) {
                fclose(f);
                return false;
            }
    if (magic[3] == '4') {
        for (int cz = 0; cz < w.ncz(); cz++)
            for (int cx = 0; cx < w.ncx(); cx++)
                if (fread(w.at(cx, cz).flow, 1, sizeof(w.at(cx, cz).flow), f) != sizeof(w.at(cx, cz).flow)) {
                    fclose(f);
                    return false;
                }
    } else {
        // старые сейвы: флюидам полный уровень
        for (int cz = 0; cz < w.ncz(); cz++)
            for (int cx = 0; cx < w.ncx(); cx++) {
                Chunk& c = w.at(cx, cz);
                for (int i = 0; i < Chunk::SX * Chunk::SY * Chunk::SZ; i++)
                    c.flow[i] = (c.blocks[i] == 6 || c.blocks[i] == 7) ? 8 : 0;
            }
    }
    fclose(f);
    return true;
}
