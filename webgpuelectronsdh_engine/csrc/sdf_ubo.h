#ifndef SDF_UBO_H
#define SDF_UBO_H
// CPU-зеркало UBO под WGSL из renderer.js-прототипа.
// WGSL std140: каждый vec3f+f32 = 16Б, весь UBO 48Б. Проверяем статически.
#include "sdf_math.h"
#include <assert.h>
#include <math.h>

typedef struct {
    Vec3 camPos;  float time;
    Vec3 camTarget; float resX;   // resX/resY вместо dummy-текстуры (баг прототипа)
    Vec3 sunDir;  float maxSteps;
    float resY; float mode; float pad[2]; // mode: 0 цвет, 1 нормали, 2 глубина
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
    // Дуга как в майне и K (день 1200с = 20 мин как их day_length).
    // Солнце — по day-часам, облака — по реальным (их cloud_time += dt).
    float a = 0.3f + t * (6.2831853f / 1200.0f);
    Vec3 s = { cosf(a), sinf(a), 0.35f };
    return v3_norm(s);
}
#endif
