#ifndef VOX_BLOCK_H
#define VOX_BLOCK_H
// Типы блоков. id влезает в u8, таблица свойств — тут же.
#include <stdint.h>

typedef enum {
    B_AIR = 0,
    B_GRASS,
    B_DIRT,
    B_STONE,
    B_LOG,
    B_LEAVES,
    B_SAND,
    B_WATER,
    B_BEDROCK,
    B_COUNT
} BlockId;

static inline int vox_solid(uint8_t id) {
    return id != B_AIR && id != B_WATER;
}
static inline int vox_opaque(uint8_t id) {
    return id != B_AIR && id != B_WATER && id != B_LEAVES;
}
static inline int vox_cutout(uint8_t id) {
    return id == B_LEAVES;
}
// Слой атласа 16x16x9 под грань: индексы их BlockRegistry
// (tile 5 = лава светится в шейдере, 6 = листва с discard).
static inline int vox_tile(uint8_t id, int axis, int sign) {
    if (id == B_GRASS) return (axis == 1 && sign > 0) ? 0 : 1;
    if (id == B_DIRT) return 2;
    if (id == B_LOG) return (axis == 1) ? 8 : 7;
    if (id == B_LEAVES) return 6;
    return 3; // stone, bedrock, sand, остальное
}
#endif
