#ifndef VOX_GEN_H
#define VOX_GEN_H
// Генерация холмов по сиду: детерминированный value-noise, 3 октавы.
// Пустыня/деревья/пещеры — позже, сейчас только рельеф + слои.
#include "vox_chunk.h"
#include <stdint.h>
#include <math.h>

#define VOX_PATCH 3 // патч 3x3 чанка = 48x64x48 вокселей
#define VOX_PW (VOX_PATCH * VOX_SX)
#define VOX_PZ (VOX_PATCH * VOX_SZ)

void vox_gen(VoxChunk *c, uint32_t seed);
// Высота поверхности в мировых XZ (та же формула, что внутри gen).
int vox_height(int wx, int wz, uint32_t seed);
// Патч чанков (cx0..cx0+2, cz0..cz0+2) плотно в dst[48*64*48], индекс (y*48+z)*48+x.
void vox_gen_patch(uint8_t *dst, int cx0, int cz0, uint32_t seed);
// CPU-зеркало DDA для отладки (клавиша C зондирует центральный луч).
// Возвращает t или -1; нормаль грани и id блока наружу.
float vox_probe(const uint8_t *vox, float ox, float oy, float oz,
                float dx, float dy, float dz, float maxT,
                float *nx, float *ny, float *nz, uint8_t *id);
// Пол для камеры: высота поверхности + 0.6 (вне патча 0.6).
static inline float vox_floor_y(float x, float z) {
    // Мир бесконечный (vox_height для любых координат): границ нет.
    // Сид обязан совпадать с vox_world_init, иначе физика и картинка разъедутся.
    int gx = (int)floorf(x), gz = (int)floorf(z);
    return (float)vox_height(gx, gz, 1337) + 0.6f;
}
// 1 если в (x,z) на высоте y свободно (холм не мешает), иначе 0.
static inline int vox_floor(float x, float z, float y) {
    return y >= vox_floor_y(x, z);
}
#endif
