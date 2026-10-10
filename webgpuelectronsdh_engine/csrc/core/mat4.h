#ifndef CORE_MAT4_H
#define CORE_MAT4_H
// Минимальные mat4 column-major под OpenGL ([-1,1] глубина).
// Общие для растровых ядер (GL сейчас, Vulkan потом).
#include <math.h>
#include <string.h>

typedef struct { float m[16]; } Mat4;

static inline Mat4 m4id(void) {
    Mat4 o;
    memset(o.m, 0, sizeof o.m);
    o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f;
    return o;
}

static inline Mat4 m4mul(const Mat4 *a, const Mat4 *b) {
    Mat4 o;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            o.m[c * 4 + r] = a->m[r] * b->m[c * 4] + a->m[4 + r] * b->m[c * 4 + 1] +
                             a->m[8 + r] * b->m[c * 4 + 2] + a->m[12 + r] * b->m[c * 4 + 3];
    return o;
}

static inline Mat4 m4persp(float fovy, float aspect, float zn, float zf) {
    Mat4 o;
    memset(o.m, 0, sizeof o.m);
    float t = tanf(fovy * 0.5f);
    o.m[0] = 1.0f / (aspect * t);
    o.m[5] = 1.0f / t;
    o.m[10] = -(zf + zn) / (zf - zn);
    o.m[11] = -1.0f;
    o.m[14] = -(2.0f * zf * zn) / (zf - zn);
    return o;
}

static inline Mat4 m4look(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = sqrtf(fx * fx + fy * fy + fz * fz);
    if (fl < 1e-9f) return m4id();
    fx /= fl; fy /= fl; fz /= fl;
    // right = norm(cross(f, up)), up = (0,1,0); вырождение сверху/снизу -> +x
    float rx = -fz, ry = 0.0f, rz = fx;
    float rl = sqrtf(rx * rx + rz * rz);
    if (rl < 1e-6f) { rx = 1.0f; ry = 0.0f; rz = 0.0f; rl = 1.0f; }
    rx /= rl; ry /= rl; rz /= rl;
    float ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
    Mat4 o = m4id();
    o.m[0] = rx; o.m[4] = ry; o.m[8] = rz;
    o.m[1] = ux; o.m[5] = uy; o.m[9] = uz;
    o.m[2] = -fx; o.m[6] = -fy; o.m[10] = -fz;
    o.m[12] = -(rx * ex + ry * ey + rz * ez);
    o.m[13] = -(ux * ex + uy * ey + uz * ez);
    o.m[14] = fx * ex + fy * ey + fz * ez;
    return o;
}

// Обратная через adjugate. det ~0 (вырождена) -> identity, не NaN.
static inline Mat4 m4inv(const Mat4 *a) {
    const float *m = a->m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
           + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
           - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
           + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
            - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
           - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
           + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
           - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
            + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
           + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
           - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
            + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
            - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
           - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
           + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
            - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
            + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[4]*inv[1] + m[8]*inv[2] + m[12]*inv[3];
    Mat4 o = m4id();
    if (fabsf(det) < 1e-12f) return o;
    for (int i = 0; i < 16; i++) o.m[i] = inv[i] / det;
    return o;
}
#endif
