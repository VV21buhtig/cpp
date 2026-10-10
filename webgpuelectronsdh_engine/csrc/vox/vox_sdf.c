// Точный знаковый EDT: сепарабельный Felzenszwalb & Huttenlocher.
// Область 48x64x16? нет — 48x64x48 (чанк + кольцо соседей: ±x,±z полно,
// y весь столб). Clamp 12 < 16 (кольцо) => после клампа ВЕЗДЕ точно.
#include "vox_sdf.h"
#include <math.h>
#include <string.h>

#define SW 48
#define SH 64
#define SD 48
#define SN ((size_t)SW * SH * SD)
#define INF 1e9f

static float work[SN];
static unsigned char occ[SN];
// Скретч 1D: n <= 64, vflt нужно n+1 -> запас.
static int vint[80];
static float vflt[80];

static size_t sidx(int x, int y, int z) {
    return ((size_t)y * SD + (size_t)z) * SW + (size_t)x;
}

static uint8_t occ_at(const VoxChunk *c, const VoxChunk *nb[6], int x, int y, int z) {
    // локальные чанка: x,z 0..15; снаружи — соседи/среда
    if (y < 0) return 1; // под миром solid (дно без протечек)
    if (y >= VOX_SY) return 0;
    if (x >= 0 && x < VOX_SX && z >= 0 && z < VOX_SZ)
        return vox_solid(c->id[vox_idx(x, y, z)]) ? 1 : 0;
    const VoxChunk *n = 0;
    int lx = x, lz = z;
    if (x < 0) { n = nb[0]; lx = x + VOX_SX; }
    else if (x >= VOX_SX) { n = nb[1]; lx = x - VOX_SX; }
    else if (z < 0) { n = nb[4]; lz = z + VOX_SZ; }
    else if (z >= VOX_SZ) { n = nb[5]; lz = z - VOX_SZ; }
    if (lx < 0 || lx >= VOX_SX || lz < 0 || lz >= VOX_SZ) return 0;
    if (!n) return 0;
    return vox_solid(n->id[vox_idx(lx, y, lz)]) ? 1 : 0;
}

// 1D квадратный EDT по Фигуре f[0..n), результат в d. Классика F&H.
static void edt1d(const float *f, float *d, int n) {
    int k = 0;
    vint[0] = 0;
    vflt[0] = -1e30f;
    vflt[1] = 1e30f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + (float)(q * q)) - (f[vint[k]] + (float)(vint[k] * vint[k]))) /
                  (float)(2 * q - 2 * vint[k]);
        while (s <= vflt[k]) {
            k--;
            s = ((f[q] + (float)(q * q)) - (f[vint[k]] + (float)(vint[k] * vint[k]))) /
                (float)(2 * q - 2 * vint[k]);
        }
        k++;
        vint[k] = q;
        vflt[k] = s;
        vflt[k + 1] = 1e30f;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (vflt[k + 1] < (float)q) k++;
        float dq = (float)(q - vint[k]);
        d[q] = dq * dq + f[vint[k]];
    }
}

static float tmpLine[80]; // max(SW,SH,SD) + запас
static float outLine[80];

// Квадрат дистанции до ближайшего вокселя со значением want (0/1).
static void edt_to(const VoxChunk *c, const VoxChunk *nb[6], int want, float *dist2) {
    for (int y = 0; y < SH; y++)
        for (int z = 0; z < SD; z++)
            for (int x = 0; x < SW; x++) {
                int lx = x - VOX_SX, lz = z - VOX_SZ;
                occ[sidx(x, y, z)] = occ_at(c, nb, lx, y, lz);
            }
    for (size_t i = 0; i < SN; i++) work[i] = (occ[i] == want) ? 0.0f : INF;
    // проход X
    for (int y = 0; y < SH; y++)
        for (int z = 0; z < SD; z++) {
            size_t b = sidx(0, y, z);
            for (int x = 0; x < SW; x++) tmpLine[x] = work[b + x];
            edt1d(tmpLine, outLine, SW);
            for (int x = 0; x < SW; x++) work[b + x] = outLine[x];
        }
    // проход Z
    for (int y = 0; y < SH; y++)
        for (int x = 0; x < SW; x++) {
            for (int z = 0; z < SD; z++) tmpLine[z] = work[sidx(x, y, z)];
            edt1d(tmpLine, outLine, SD);
            for (int z = 0; z < SD; z++) work[sidx(x, y, z)] = outLine[z];
        }
    // проход Y
    for (int z = 0; z < SD; z++)
        for (int x = 0; x < SW; x++) {
            for (int y = 0; y < SH; y++) tmpLine[y] = work[sidx(x, y, z)];
            edt1d(tmpLine, outLine, SH);
            for (int y = 0; y < SH; y++) work[sidx(x, y, z)] = outLine[y];
        }
    memcpy(dist2, work, SN * sizeof(float));
}

static float distSolid[SN];
static float distAir[SN];

void vox_sdf_bake(const VoxChunk *c, const VoxChunk *nb[6], float *out) {
    edt_to(c, nb, 1, distSolid); // воздух: +до solid
    edt_to(c, nb, 0, distAir);   // solid: -до air
    for (int y = 0; y < VOX_SY; y++)
        for (int z = 0; z < VOX_SZ; z++)
            for (int x = 0; x < VOX_SX; x++) {
                size_t g = sidx(x + VOX_SX, y, z + VOX_SZ);
                size_t o = vox_idx(x, y, z);
                float d = vox_solid(c->id[o])
                    ? -sqrtf(distAir[g])
                    : sqrtf(distSolid[g]);
                if (d > VOX_SDF_R) d = VOX_SDF_R;
                if (d < -VOX_SDF_R) d = -VOX_SDF_R;
                out[o] = d;
            }
}
