#ifndef SDF_SCENE_H
#define SDF_SCENE_H
// Параметры сцены — один источник для CPU-зеркала.
// Должны совпадать с map() в shaders/sdf.wgsl (блоб/smin/box/hole/plane).
// Расхождение = guard врёт и камера ныряет внутрь -> белый экран.
#include "sdf_math.h"
#include <math.h>

#define SDF_BOX_CY 0.59f // низ -0.01: закопан, копланарности с plane нет

static inline float sdf_smin(float a, float b, float k) {
    float h = fminf(1.0f, fmaxf(0.0f, 0.5f + 0.5f * (b - a) / k));
    return b * (1.0f - h) + a * h - k * h * (1.0f - h);
}

// Копия map() без material id: только дистанция (нужна guard'у).
static inline float sdf_eval(float px, float py, float pz) {
    float d = py; // plane y=0
    float dx1 = px + 1.2f, dy1 = py - 1.0f, dz1 = pz;
    float s1 = sqrtf(dx1*dx1 + dy1*dy1 + dz1*dz1) - 1.0f;
    float dx2 = px - 1.2f, dy2 = py - 0.8f, dz2 = pz - 0.5f;
    float s2 = sqrtf(dx2*dx2 + dy2*dy2 + dz2*dz2) - 0.7f;
    float blob = sdf_smin(s1, s2, 0.6f);
    if (blob < d) d = blob;
    float qx = fabsf(px) - 0.8f, qy = fabsf(py - SDF_BOX_CY) - 0.6f, qz = fabsf(pz + 2.0f) - 0.8f;
    float ox = fmaxf(qx, 0.0f), oy = fmaxf(qy, 0.0f), oz = fmaxf(qz, 0.0f);
    float bx = sqrtf(ox*ox + oy*oy + oz*oz) + fminf(fmaxf(qx, fmaxf(qy, qz)), 0.0f);
    if (bx < d) d = bx;
    float hx = px, hy = py - 1.4f, hz = pz - 1.8f;
    float hole = sqrtf(hx*hx + hy*hy + hz*hz) - 0.5f;
    float res = d > -hole ? d : -hole; // opS
    return res;
}

// Guard камеры: пока внутри (< 0.35) — тянем к таргету. Порт цикла из renderer.js.
// 32 итерации: со старта под полом (dist 27 + низкий pitch = y -1.7) восьми
// не хватало, камера застревала в 3см над plane -> скользящий марш выедал шаги.
static inline void sdf_guard(float *cx, float *cy, float *cz,
                             float tx, float ty, float tz) {
    for (int g = 0; g < 32 && sdf_eval(*cx, *cy, *cz) < 0.35f; g++) {
        *cx += (tx - *cx) * 0.12f;
        *cy += (ty - *cy) * 0.12f;
        *cz += (tz - *cz) * 0.12f;
    }
}
#endif
