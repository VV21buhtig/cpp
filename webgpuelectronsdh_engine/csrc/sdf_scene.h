#ifndef SDF_SCENE_H
#define SDF_SCENE_H
// Шаг 2: куб + шар стоят на полу. Совпадает с map() в shaders/sdf.wgsl.
#include "sdf_math.h"
#include <math.h>

static inline float sdf_eval(float px, float py, float pz) {
    float d = py; // пол
    float qx = fabsf(px) - 0.8f, qy = fabsf(py - 0.8f) - 0.8f, qz = fabsf(pz) - 0.8f;
    float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f), oz = fmaxf(qz, 0.0f);
    float bx = sqrtf(ox*ox + oy*oy + oz*oz) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
    if (bx < d) d = bx;
    float dx = px - 1.8f, dy = py - 0.7f;
    float s = sqrtf(dx*dx + dy*dy + pz*pz) - 0.7f;
    return d < s ? d : s;
}

// Guard орбиты: пока внутри (< 0.35) — тянем к таргету (sdf_native без GPU).
static inline void sdf_guard(float *cx, float *cy, float *cz,
                             float tx, float ty, float tz) {
    for (int g = 0; g < 32 && sdf_eval(*cx, *cy, *cz) < 0.35f; g++) {
        *cx += (tx - *cx) * 0.12f;
        *cy += (ty - *cy) * 0.12f;
        *cz += (tz - *cz) * 0.12f;
    }
}
// Guard отлёта: внутри геометрии (< 0.35) — пятимся назад по взгляду, max 32.
static inline void sdf_guard_fly(float *cx, float *cy, float *cz, Vec3 fwd) {
    for (int g = 0; g < 32 && sdf_eval(*cx, *cy, *cz) < 0.35f; g++) {
        *cx -= fwd.x * 0.25f;
        *cy -= fwd.y * 0.25f;
        *cz -= fwd.z * 0.25f;
    }
}
#endif
