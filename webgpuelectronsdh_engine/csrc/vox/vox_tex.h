#ifndef VOX_TEX_H
#define VOX_TEX_H
// Атлас блоков 16x16x7 RGBA из voxel-render/texture/tiles (их тайлы 1:1).
// Слои: 0 grass_top, 1 grass_side, 2 dirt, 3 stone, 4 log_side, 5 log_top, 6 leaves.
// Бедрок едет слоем stone (видят единицу).
#include <stdint.h>

#define VOX_TILE 16
#define VOX_LAYERS 7
#define VOX_TEXELS (VOX_TILE * VOX_TILE * VOX_LAYERS)

// Грузит tiles/<name>.png 16x16 RGBA в layers. 1 ок, 0 провал (тогда фолбэк-плоско).
int vox_tex_load(const char *dir, uint8_t *out);
#endif
