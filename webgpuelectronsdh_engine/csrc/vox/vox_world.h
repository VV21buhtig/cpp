#ifndef VOX_WORLD_H
#define VOX_WORLD_H
// Мир: пул чанков + кольцо вокруг игрока. Без мешей: DDA читает воксели
// напрямую, поэтому стриминг = генерация + выселение, каллинг не нужен
// (луч сам пролетает пустоту). Загрузка в GPU — следующим шагом.
#include "vox_chunk.h"
#include <stdint.h>

#define VOX_POOL 144   // 12x12 слотов по 16КБ = 2.3МБ статикой, без malloc
#define VOX_RADIUS 4   // кольцо 9x9 = 81 чанк вокруг игрока
#define VOX_GEN_BUDGET 4 // генераций за кадр максимум (не фризим кадр)

typedef struct {
    int used;
    int cx, cz;
    int dirty; // 1 если сгенерён, но app ещё не залил в GPU (гасится после upload_chunk)
    VoxChunk data;
} VoxSlot;

typedef struct {
    VoxSlot slots[VOX_POOL];
    int count; // занято слотов
    uint32_t seed;
    int genLast; // сгенерировано за последний ensure (для тестов/лога)
} VoxWorld;

void vox_world_init(VoxWorld *w, uint32_t seed);
// Держать кольцо (pcx,pcz) — центр чанка игрока. Возвращает сгенерировано (<=BUDGET).
int vox_world_ensure(VoxWorld *w, int pcx, int pcz);
// Найти резидентный чанк, 0 если выгружен.
VoxChunk *vox_world_find(VoxWorld *w, int cx, int cz);
#endif
