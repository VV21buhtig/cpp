#ifndef ENGINE_VOX_FEED_H
#define ENGINE_VOX_FEED_H

#ifdef __cplusplus
extern "C" {
#endif

// Воксельный фидер: чанки -> textured-меши -> слоты ядра.
// Ядро (gl/vk-растр) чанков не знает: сюда прилетают готовые вершины
// (12 float) и матрицы. Соседство/швы решает фидер, не ядро.
// Состояние слотов — внутри (static, как маски мешера).
// Две операции стриминга: залить один + снести по координатам.
#include "render_api.h"
#include "vox/vox_chunk.h"

#define VOX_FEED_SLOTS 256

// Смешить чанк (соседи NULL=воздух) и залить в слот ядра.
// Возвращает слот (>=0) или -1 (пул полон / ядро отказало).
int vox_feed_one(RenderCore *rc, const VoxChunk *chunk, const VoxChunk *nb[6]);
// Снести меш чанка из ядра (стал не нужен). Молчит если не было.
void vox_feed_drop(RenderCore *rc, int cx, int cz);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_VOX_FEED_H
