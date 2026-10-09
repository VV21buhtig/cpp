#include "vox_gen.h"

static uint32_t hash2(int x, int z, uint32_t seed) {
    uint32_t h = (uint32_t)(x * 0x27d4eb2d) ^ (uint32_t)(z * 0x9e3779b1) ^ (seed * 0x165667b1);
    h *= 0x85ebca6b;
    h ^= h >> 16;
    return h;
}

static float vnoise(int x, int z, uint32_t seed) {
    int ix = x >= 0 ? x / 8 : (x - 7) / 8;
    int iz = z >= 0 ? z / 8 : (z - 7) / 8;
    float fx = (float)(x - ix * 8) / 8.0f;
    float fz = (float)(z - iz * 8) / 8.0f;
    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uz = fz * fz * (3.0f - 2.0f * fz);
    float a = (float)(hash2(ix, iz, seed) & 1023u) / 1024.0f;
    float b = (float)(hash2(ix + 1, iz, seed) & 1023u) / 1024.0f;
    float c = (float)(hash2(ix, iz + 1, seed) & 1023u) / 1024.0f;
    float d = (float)(hash2(ix + 1, iz + 1, seed) & 1023u) / 1024.0f;
    return a + (b - a) * ux + (c - a) * uz + (a - b - c + d) * ux * uz;
}

int vox_height(int wx, int wz, uint32_t seed) {
    float h = 20.0f
        + 12.0f * vnoise(wx, wz, seed)
        + 6.0f * vnoise(wx * 2 + 13, wz * 2 + 7, seed ^ 0x9e37)
        + 3.0f * vnoise(wx * 4 + 71, wz * 4 + 37, seed ^ 0x51f3);
    if (h < 2.0f) h = 2.0f;
    if (h > VOX_SY - 4) h = (float)(VOX_SY - 4);
    return (int)h;
}

void vox_gen(VoxChunk *c, uint32_t seed) {
    for (int i = 0; i < VOX_N; i++) c->id[i] = B_AIR;
    for (int z = 0; z < VOX_SZ; z++) {
        for (int x = 0; x < VOX_SX; x++) {
            int wx = c->cx * VOX_SX + x;
            int wz = c->cz * VOX_SZ + z;
            int h = vox_height(wx, wz, seed);
            for (int y = 0; y < h - 3; y++) vox_set(c, x, y, z, B_STONE);
            for (int y = h - 3; y < h; y++) vox_set(c, x, y, z, B_DIRT);
            vox_set(c, x, h, z, B_GRASS);
            vox_set(c, x, 0, z, B_BEDROCK);
        }
    }
}

void vox_gen_patch(uint8_t *dst, int cx0, int cz0, uint32_t seed) {
    VoxChunk c;
    for (int pz = 0; pz < VOX_PATCH; pz++) {
        for (int px = 0; px < VOX_PATCH; px++) {
            c.cx = cx0 + px;
            c.cz = cz0 + pz;
            vox_gen(&c, seed);
            for (int y = 0; y < VOX_SY; y++) {
                for (int z = 0; z < VOX_SZ; z++) {
                    for (int x = 0; x < VOX_SX; x++) {
                        int wx = px * VOX_SX + x;
                        int wz = pz * VOX_SZ + z;
                        dst[((size_t)y * VOX_PZ + (size_t)wz) * VOX_PW + (size_t)wx] =
                            c.id[vox_idx(x, y, z)];
                    }
                }
            }
        }
    }
}
