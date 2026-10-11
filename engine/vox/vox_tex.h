#ifndef VOX_TEX_H
#define VOX_TEX_H
// Атлас слоёв — индексы 1:1 с их BlockRegistry (NOT ours!):
// 0 grass_top 1 grass_side 2 dirt 3 stone 4 water 5 lava 6 leaves 7 log_side 8 log_top.
// Совпадение нужно их lighting.fs: tile 5 светится (лава), 6 discard по альфе.
#include <stdint.h>

#define VOX_TILE 16
#define VOX_LAYERS 9
#define VOX_TEXELS (VOX_TILE * VOX_TILE * VOX_LAYERS)

// Грузит tiles/<name>.png 16x16 RGBA в layers. 1 ок, 0 провал (тогда фолбэк-плоско).
int vox_tex_load(const char *dir, uint8_t *out);
#endif
