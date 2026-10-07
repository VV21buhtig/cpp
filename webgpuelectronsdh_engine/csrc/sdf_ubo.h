#ifndef SDF_UBO_H
#define SDF_UBO_H
// CPU-зеркало UBO под WGSL из renderer.js-прототипа.
// WGSL std140: каждый vec3f+f32 = 16Б, весь UBO 48Б. Проверяем статически.
#include "sdf_math.h"
#include <assert.h>

typedef struct {
    Vec3 camPos;  float time;
    Vec3 camTarget; float resX;   // resX/resY вместо dummy-текстуры (баг прототипа)
    Vec3 sunDir;  float maxSteps;
    float resY; float pad[3];    // добивка до 64Б для queue.writeBuffer кратности
} SdfUBO;

_Static_assert(sizeof(SdfUBO) == 64, "SdfUBO must be 64B");

// Камера orbit как в прототипе: yaw/pitch/dist/target (vulcan CamCtl идея,
// но БЕЗ его мыши-захвата: здесь orbit, не free-fly).
static inline void sdf_camera_orbit(Vec3 target, float yaw, float pitch, float dist, Vec3 *outPos) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
    outPos->x = target.x + dist * cp * cy;
    outPos->y = target.y + dist * sp;
    outPos->z = target.z + dist * cp * sy;
}

static inline Vec3 sdf_sun(float t) {
    // как vulcan frame.cpp: sun=(cos(tod),sin(tod),0.35), здесь медленный дрейф
    float sa = t * 0.05f;
    Vec3 s = { cosf(sa), 0.55f, sinf(sa) };
    return v3_norm(s);
}
#endif
