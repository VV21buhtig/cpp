#include "engine/save.h"
#include "engine/world.h"
#include <cstdio>

bool saveWorld(const World& w, const char* path) {
    // Атомарно: пишем в tmp + rename, чтобы краш посреди записи не убил сейв.
    // Файл ~37 КБ, запись только по F5 — SSD не заметит (TBW сотни ТБ).
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    const char magic[4] = {'V', 'X', 'W', '1'};
    bool ok = true;
    ok &= fwrite(magic, 1, 4, f) == 4;
    int dims[5] = {World::CX, World::CZ, Chunk::SX, Chunk::SY, Chunk::SZ};
    ok &= fwrite(dims, sizeof(int), 5, f) == 5;
    for (int cz = 0; cz < World::CZ && ok; cz++)
        for (int cx = 0; cx < World::CX && ok; cx++)
            ok &= fwrite(w.chunks[cx][cz].blocks, 1, sizeof(w.chunks[cx][cz].blocks), f) == sizeof(w.chunks[cx][cz].blocks);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}

bool loadWorld(World& w, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || magic[0] != 'V' || magic[1] != 'X' || magic[2] != 'W' || magic[3] != '1') {
        fclose(f);
        return false;
    }
    int dims[5];
    if (fread(dims, sizeof(int), 5, f) != 5 || dims[0] != World::CX || dims[1] != World::CZ ||
        dims[2] != Chunk::SX || dims[3] != Chunk::SY || dims[4] != Chunk::SZ) {
        fclose(f);
        return false;
    }
    for (int cz = 0; cz < World::CZ; cz++)
        for (int cx = 0; cx < World::CX; cx++)
            if (fread(w.chunks[cx][cz].blocks, 1, sizeof(w.chunks[cx][cz].blocks), f) != sizeof(w.chunks[cx][cz].blocks)) {
                fclose(f);
                return false;
            }
    fclose(f);
    return true;
}
