// Сторож общей математики растровых ядер (+ конвенция vk_conv.h п.4:
// глубина Vulkan [0,1]). Падает текстом, а не перевёрнутой картинкой.
#include <stdio.h>
#include "mat4.h"

static float ndc_z(const Mat4 *p, float ez) {
    float cz = p->m[10] * ez + p->m[14];
    float cw = p->m[11] * ez + p->m[15];
    return cz / cw;
}

int main(void) {
    int bad = 0;
    Mat4 g = m4persp(1.2f, 16.0f / 9.0f, 0.1f, 600.0f);
    float gn = ndc_z(&g, -0.1f), gf = ndc_z(&g, -600.0f);
    printf("gl: near=%.4f far=%.4f (want -1/+1)\n", gn, gf);
    if (gn < -1.01f || gn > -0.99f || gf < 0.99f || gf > 1.01f) { printf("GL PERSP FAIL\n"); bad = 1; }
    Mat4 k = m4persp_vk(1.2f, 16.0f / 9.0f, 0.1f, 600.0f);
    float kn = ndc_z(&k, -0.1f), kf = ndc_z(&k, -600.0f);
    printf("vk: near=%.4f far=%.4f (want 0/+1)\n", kn, kf);
    if (kn < -0.01f || kn > 0.01f || kf < 0.99f || kf > 1.01f) { printf("VK PERSP FAIL\n"); bad = 1; }
    // lookAt: цель строго по -Z (дистанция какая есть — проверяем ось и длину).
    Mat4 v = m4look(32, 42, 12, 31.5f, 41.3f, 12.6f);
    float tx = 31.5f, ty = 41.3f, tz = 12.6f;
    float ox = v.m[0] * tx + v.m[4] * ty + v.m[8] * tz + v.m[12];
    float oy = v.m[1] * tx + v.m[5] * ty + v.m[9] * tz + v.m[13];
    float oz = v.m[2] * tx + v.m[6] * ty + v.m[10] * tz + v.m[14];
    float dl = sqrtf(ox * ox + oy * oy + oz * oz);
    float want = sqrtf((tx - 32) * (tx - 32) + (ty - 42) * (ty - 42) + (tz - 12) * (tz - 12));
    printf("look: (%.3f,%.3f,%.3f) |.|=%.3f (want 0,0,-d=%.3f)\n", ox, oy, oz, dl, want);
    if (ox > 0.01f || ox < -0.01f || oy > 0.01f || oy < -0.01f ||
        oz > 0.0f || dl < want - 0.01f || dl > want + 0.01f) {
        printf("LOOK FAIL\n");
        bad = 1;
    }
    printf(bad ? "MAT4 FAIL\n" : "MAT4 OK\n");
    return bad;
}
