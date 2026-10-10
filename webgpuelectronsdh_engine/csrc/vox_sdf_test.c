// Тест SDF-бейка: точный EDT на синтетике + знак/кламп на реальном чанке.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "vox/vox_gen.h"
#include "vox/vox_sdf.h"

static int bad = 0;
static void check(int cond, const char *msg) {
    if (!cond) { printf("SDF FAIL: %s\n", msg); bad = 1; }
}

int main(void) {
    static float out[VOX_N];
    // 1. Синтетика: один solid в пустоте — целые дистанции и диагональ 5.
    {
        VoxChunk c;
        memset(&c, 0, sizeof c);
        c.id[vox_idx(8, 32, 8)] = B_STONE;
        const VoxChunk *nb[6] = {0, 0, 0, 0, 0, 0};
        vox_sdf_bake(&c, nb, out);
        check(fabsf(out[vox_idx(8, 32, 8)] + 1.0f) < 0.01f, "solid self");
        check(fabsf(out[vox_idx(11, 32, 8)] - 3.0f) < 0.01f, "axis 3");
        check(fabsf(out[vox_idx(8, 32, 13)] - 5.0f) < 0.01f, "axis 5");
        check(fabsf(out[vox_idx(11, 36, 8)] - 5.0f) < 0.01f, "diag 3-4-5");
        check(fabsf(out[vox_idx(0, 0, 0)] - VOX_SDF_R) < 0.01f, "clamp");
    }
    // 2. Реальный чанк: знак + кламп везде.
    {
        VoxChunk c;
        c.cx = 0; c.cz = 0;
        vox_gen(&c, 1337);
        const VoxChunk *nb[6] = {0, 0, 0, 0, 0, 0};
        vox_sdf_bake(&c, nb, out);
        for (int i = 0; i < VOX_N; i++) {
            int solid = vox_solid(c.id[i]);
            if (solid && out[i] > 0.001f) { check(0, "sign solid"); break; }
            if (!solid && out[i] < -0.001f) { check(0, "sign air"); break; }
            if (fabsf(out[i]) > VOX_SDF_R + 0.001f) { check(0, "clamp range"); break; }
        }
    }
    printf(bad ? "SDFTEST FAIL\n" : "SDFTEST OK\n");
    return bad;
}
