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

void vox_gen_patch(uint8_t *dst, int cx0, int cz0, uint32_t seed) {    VoxChunk c;
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

static uint8_t probe_at(const uint8_t *vox, int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= VOX_PW || y >= VOX_SY || z >= VOX_PZ) return B_AIR;
    return vox[((size_t)y * VOX_PZ + (size_t)z) * VOX_PW + (size_t)x];
}

float vox_probe(const uint8_t *vox, float ox, float oy, float oz,
                float dx, float dy, float dz, float maxT,
                float *nx, float *ny, float *nz, uint8_t *id) {
    *nx = 0; *ny = 0; *nz = 0; *id = 0;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-9f) return -1.0f;
    dx /= len; dy /= len; dz /= len;
    // Вход в бокс (slab).
    float tEnter = 0.0f, tExit = maxT;
    {
        float t0x = (0.0f - ox) / dx, t1x = (48.0f - ox) / dx;
        float t0y = (0.0f - oy) / dy, t1y = (64.0f - oy) / dy;
        float t0z = (0.0f - oz) / dz, t1z = (48.0f - oz) / dz;
        float mnx = t0x < t1x ? t0x : t1x, mxx = t0x > t1x ? t0x : t1x;
        float mny = t0y < t1y ? t0y : t1y, mxy = t0y > t1y ? t0y : t1y;
        float mnz = t0z < t1z ? t0z : t1z, mxz = t0z > t1z ? t0z : t1z;
        float en = mnx > mny ? (mnx > mnz ? mnx : mnz) : (mny > mnz ? mny : mnz);
        float ex = mxx < mxy ? (mxx < mxz ? mxx : mxz) : (mxy < mxz ? mxy : mxz);
        if (en > ex || ex < 0.0f) return -1.0f;
        if (en > 0.0f) tEnter = en;
        if (ex < tExit) tExit = ex;
    }
    int px = (int)floorf(ox + dx * tEnter);
    int py = (int)floorf(oy + dy * tEnter);
    int pz = (int)floorf(oz + dz * tEnter);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1, sz = dz > 0 ? 1 : -1;
    float tdx = fabsf(dx) > 1e-9f ? fabsf(1.0f / dx) : 1e9f;
    float tdy = fabsf(dy) > 1e-9f ? fabsf(1.0f / dy) : 1e9f;
    float tdz = fabsf(dz) > 1e-9f ? fabsf(1.0f / dz) : 1e9f;
    float idx = fabsf(dx) > 1e-9f ? 1.0f / dx : 1e9f;
    float idy = fabsf(dy) > 1e-9f ? 1.0f / dy : 1e9f;
    float idz = fabsf(dz) > 1e-9f ? 1.0f / dz : 1e9f;
    float tmx = ((sx > 0 ? (float)(px + 1) : (float)px) - ox) * idx;
    float tmy = ((sy > 0 ? (float)(py + 1) : (float)py) - oy) * idy;
    float tmz = ((sz > 0 ? (float)(pz + 1) : (float)pz) - oz) * idz;
    float t = tEnter;
    for (int i = 0; i < 256; i++) {
        if (tmx < tmy && tmx < tmz) {
            px += sx; t = tmx; tmx += tdx;
            *nx = -(float)sx; *ny = 0; *nz = 0;
        } else if (tmy < tmz) {
            py += sy; t = tmy; tmy += tdy;
            *nx = 0; *ny = -(float)sy; *nz = 0;
        } else {
            pz += sz; t = tmz; tmz += tdz;
            *nx = 0; *ny = 0; *nz = -(float)sz;
        }
        if (t > tExit) return -1.0f;
        uint8_t v = probe_at(vox, px, py, pz);
        if (v != B_AIR) { *id = v; return t; }
    }
    return -1.0f;
}
