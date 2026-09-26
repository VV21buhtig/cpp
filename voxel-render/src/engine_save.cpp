#include "engine/save.h"
#include "engine/world.h"
#include <cstdio>

bool saveWorld(const World& w, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const char magic[4] = {'V', 'X', 'W', '1'};
    fwrite(magic, 1, 4, f);
    int dims[5] = {World::CX, World::CZ, Chunk::SX, Chunk::SY, Chunk::SZ};
    fwrite(dims, sizeof(int), 5, f);
    for (int cz = 0; cz < World::CZ; cz++)
        for (int cx = 0; cx < World::CX; cx++)
            fwrite(w.chunks[cx][cz].blocks, 1, sizeof(w.chunks[cx][cz].blocks), f);
    fclose(f);
    return true;
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
