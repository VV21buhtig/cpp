#ifndef VOX_GEN_H
#define VOX_GEN_H
// Генерация холмов по сиду: детерминированный value-noise, 3 октавы.
// Пустыня/деревья/пещеры — позже, сейчас только рельеф + слои.
#include "vox_chunk.h"
#include <stdint.h>

#define VOX_PATCH 3 // патч 3x3 чанка = 48x64x48 вокселей
#define VOX_PW (VOX_PATCH * VOX_SX)
#define VOX_PZ (VOX_PATCH * VOX_SZ)

void vox_gen(VoxChunk *c, uint32_t seed);
// Высота поверхности в мировых XZ (та же формула, что внутри gen).
int vox_height(int wx, int wz, uint32_t seed);
// Патч чанков (cx0..cx0+2, cz0..cz0+2) плотно в dst[48*64*48], индекс (y*48+z)*48+x.
void vox_gen_patch(uint8_t *dst, int cx0, int cz0, uint32_t seed);
#endif
