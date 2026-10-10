#ifndef VOX_SDF_H
#define VOX_SDF_H
// SDF-сервис (не ядро!): точное знаковое расстояние воксель→граница.
// Как MDF в AAA: объёмная копия мира для света (мягкие тени, AO, коллизии),
// первичная видимость остаётся растру/DDA. Бэк точным EDT (Felzenszwalb),
// не BFS-приближением: ближнее поле попиксельно верное.
// Вход: чанк + 6 соседей (та же схема что мешер). Выход: out[VOX_N] f32,
// solid<0, clamp ±VOX_SDF_R. Сосед NULL = воздух, y<0 = solid, y>=64 = air.
#include "vox_chunk.h"

#define VOX_SDF_R 12.0f

void vox_sdf_bake(const VoxChunk *c, const VoxChunk *nb[6], float *out);
#endif
