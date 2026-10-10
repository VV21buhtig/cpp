#ifndef ENGINE_VEC_H
#define ENGINE_VEC_H
// Векторное ядро движка: свой vec3 без glm (детерминизм).
// Чистая математика, ноль игровых зависимостей.
#include <math.h>

typedef struct { float x, y, z; } Vec3;

static inline Vec3 v3(float x, float y, float z) { Vec3 v = {x, y, z}; return v; }
static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_mul(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3_cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(Vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
static inline Vec3 v3_norm(Vec3 a) {
    float l = v3_len(a);
    return l > 1e-8f ? v3_mul(a, 1.0f / l) : v3(0, 0, 0);
}
// Базис камеры из yaw/pitch — общий для всех ядер (GL/Vulkan переиспользуют).
static inline void cam_basis(float yaw, float pitch, Vec3 *f, Vec3 *r, Vec3 *u) {
    float cp = cosf(pitch);
    *f = v3(cp * cosf(yaw), sinf(pitch), cp * sinf(yaw));
    *r = v3_norm(v3_cross(*f, v3(0.0f, 1.0f, 0.0f)));
    *u = v3_cross(*r, *f);
}
#endif
