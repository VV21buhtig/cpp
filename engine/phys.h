#ifndef ENGINE_PHYS_H
#define ENGINE_PHYS_H

#ifdef __cplusplus
extern "C" {
#endif

// L4: базовая физика (НЕ игровая!). Общая гравитация + интегратор,
// конфиг на домен. Контакты, отклики, вода, свет — дело доменов и игры:
// здесь только движение. Чистый CPU, без окна и GPU.
#include "vec.h"

typedef struct {
    float gravity;   // м/с^2, обычно отрицательная (вниз по Y)
    float maxFall;   // предельная скорость падения (+число, 0 = без предела)
} PhysDomainCfg;

// Один шаг тела: полунеявный Эйлер (стабильнее явного при больших dt).
static inline void phys_step(Vec3 *pos, Vec3 *vel, const PhysDomainCfg *cfg, float dt) {
    vel->y += cfg->gravity * dt;
    if (cfg->maxFall > 0.0f && vel->y < -cfg->maxFall) vel->y = -cfg->maxFall;
    pos->x += vel->x * dt;
    pos->y += vel->y * dt;
    pos->z += vel->z * dt;
}

// Фиксированный шаг симуляции поверх плавающего кадра.
// Возвращает число тиков (0..5, больше режем — спираль смерти не нужна).
typedef struct {
    double acc;
} PhysClock;

static inline int phys_ticks(PhysClock *c, double frameDt, double step) {
    if (frameDt < 0.0) frameDt = 0.0;
    if (frameDt > 0.25) frameDt = 0.25;
    c->acc += frameDt;
    int n = 0;
    while (c->acc >= step && n < 5) {
        c->acc -= step;
        n++;
    }
    if (n == 5) c->acc = 0.0;
    return n;
}

#ifdef __cplusplus
}
#endif
#endif // ENGINE_PHYS_H
