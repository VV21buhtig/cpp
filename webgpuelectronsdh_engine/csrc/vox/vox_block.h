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
#endif
