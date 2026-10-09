#ifndef SDF_SCENE_H
#define SDF_SCENE_H
// Шаг 0: ОДИН КУБ (центр 0, полуребро 0.8). Совпадает с map() в shaders/sdf.wgsl.
// Лестница: куб -> +шар -> +пол -> +тени -> +небо. Зеркало правится вместе с WGSL.
#include "sdf_math.h"
#include <math.h>

static inline float sdf_eval(float px, float py, float pz) {
    float qx = fabsf(px) - 0.8f, qy = fabsf(py) - 0.8f, qz = fabsf(pz) - 0.8f;
    float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f), oz = fmaxf(qz, 0.0f);
    return sqrtf(ox*ox + oy*oy + oz*oz) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
}

// Guard камеры: пока внутри (< 0.35) — тянем к таргету.
static inline void sdf_guard(float *cx, float *cy, float *cz,
                             float tx, float ty, float tz) {
    for (int g = 0; g < 32 && sdf_eval(*cx, *cy, *cz) < 0.35f; g++) {
        *cx += (tx - *cx) * 0.12f;
        *cy += (ty - *cy) * 0.12f;
        *cz += (tz - *cz) * 0.12f;
    }
}
#endif
