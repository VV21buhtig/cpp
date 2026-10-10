#ifndef VOX_CHUNK_H
#define VOX_CHUNK_H
// Чанк 16x16x64 = 16КБ, плотный массив. y вверх, индекс (y*SZ+z)*SX+x.
#include "vox_block.h"
#include <stddef.h>

#define VOX_SX 16
#define VOX_SY 64
#define VOX_SZ 16
#define VOX_N (VOX_SX * VOX_SY * VOX_SZ)

typedef struct VoxChunk {
    int cx, cz; // координаты чанка
    uint8_t id[VOX_N];
} VoxChunk;

static inline size_t vox_idx(int x, int y, int z) {
    return ((size_t)y * VOX_SZ + (size_t)z) * VOX_SX + (size_t)x;
}
static inline int vox_in(int x, int y, int z) {
    return (unsigned)x < VOX_SX && (unsigned)y < VOX_SY && (unsigned)z < VOX_SZ;
}
static inline uint8_t vox_get(const VoxChunk *c, int x, int y, int z) {
    if (!vox_in(x, y, z)) return B_AIR;
    return c->id[vox_idx(x, y, z)];
}
static inline void vox_set(VoxChunk *c, int x, int y, int z, uint8_t id) {
    if (!vox_in(x, y, z)) return;
    c->id[vox_idx(x, y, z)] = id;
}
#endif
