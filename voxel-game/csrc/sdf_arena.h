#ifndef SDF_ARENA_H
#define SDF_ARENA_H
// Линейная арена под кадр: сброс в конце кадра, ноль malloc в лупе.
// Анти-урок vulcan: общие буферы на свет/глаза приводили к ползущим границам
// (frame.cpp: каллинг выключен — indBuf делился). Здесь арены РАЗДЕЛЬНЫЕ.
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t *base; size_t cap, off; } Arena;

static inline void arena_init(Arena *a, uint8_t *mem, size_t cap) {
    a->base = mem; a->cap = cap; a->off = 0;
}
static inline void arena_reset(Arena *a) { a->off = 0; }
static inline void *arena_alloc(Arena *a, size_t n) {
    // выравнивание 16 под UBO std140/WGSL
    size_t aligned = (a->off + 15) & ~(size_t)15;
    if (aligned + n > a->cap) return 0;
    void *p = a->base + aligned;
    a->off = aligned + n;
    return p;
}
#endif
