#ifndef VOX_GEN_H
#define VOX_GEN_H
// Генерация холмов по сиду: детерминированный value-noise, 3 октавы.
// Пустыня/деревья/пещеры — позже, сейчас только рельеф + слои.
#include "vox_chunk.h"
#include <stdint.h>

void vox_gen(VoxChunk *c, uint32_t seed);
// Высота поверхности в мировых XZ (та же формула, что внутри gen).
int vox_height(int wx, int wz, uint32_t seed);
#endif
