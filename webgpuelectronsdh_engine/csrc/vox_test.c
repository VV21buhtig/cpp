// Тест шага 1: генерим чанк, печатаем боковой срез z=8 + статистику.
// Без рендера: данные первее картинки.
#include <stdio.h>
#include "vox/vox_gen.h"
#include "vox/vox_mesh.h"

static char glyph(uint8_t id) {
    switch (id) {
    case B_GRASS: return '"';
    case B_DIRT: return '+';
    case B_STONE: return '#';
    case B_BEDROCK: return 'X';
    case B_LOG: return 'T';
    case B_LEAVES: return '*';
    default: return ' ';
    }
}

int main(void) {
    VoxChunk c;
    c.cx = 0; c.cz = 0;
    vox_gen(&c, 1337);
    int counts[B_COUNT] = {0};
    for (int i = 0; i < VOX_N; i++) counts[c.id[i]]++;
    printf("chunk(0,0) seed=1337: air=%d grass=%d dirt=%d stone=%d\n",
        counts[B_AIR], counts[B_GRASS], counts[B_DIRT], counts[B_STONE]);
    printf("--- side cut z=8 (y=40..0) ---\n");
    for (int y = 40; y >= 0; y--) {
        for (int x = 0; x < VOX_SX; x++) putchar(glyph(vox_get(&c, x, y, 8)));
        putchar('\n');
    }
    // Детерминизм: та же колонка дважды + соседний чанк стыкуется по высоте.
    VoxChunk c2;
    c2.cx = 1; c2.cz = 0;
    vox_gen(&c2, 1337);
    int ok = 1;
    for (int z = 0; z < VOX_SZ; z++) {
        if (vox_get(&c, 15, vox_height(15, z, 1337), z) != B_GRASS) ok = 0;
        if (vox_get(&c2, 0, vox_height(16, z, 1337), z) != B_GRASS) ok = 0;
    }
    int h0 = vox_height(3, 5, 1337);
    printf("determinism: h(3,5)=%d (twice %d) seam=%s\n",
        h0, vox_height(3, 5, 1337), ok ? "OK" : "FAIL");
    // Меш: страйд 12, вершины в границах чанка, соседние чанки для AO/граней.
    {
        VoxChunk nbs[6];
        const VoxChunk *nb[6] = {0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 6; i++) { nbs[i].cx = 0; nbs[i].cz = 0; }
        VoxMeshOut m = {0, 0, 0};
        vox_mesh_build(&c, nb, &m);
        int bad = 0;
        if (m.n % 12 != 0) { printf("MESH STRIDE FAIL\n"); bad = 1; }
        for (size_t i = 0; i + 12 <= m.n && !bad; i += 12) {
            float x = m.v[i], y = m.v[i + 1], z = m.v[i + 2];
            if (x < -0.01f || x > 16.01f || y < -0.01f || y > 64.01f || z < -0.01f || z > 16.01f) {
                printf("MESH BOUNDS FAIL (%g,%g,%g)\n", x, y, z);
                bad = 1;
            }
            float nx = m.v[i + 3], ny = m.v[i + 4], nz = m.v[i + 5];
            float ax = nx < 0 ? -nx : nx, ay = ny < 0 ? -ny : ny, az = nz < 0 ? -nz : nz;
            if ((ax + ay + az < 0.99f) || m.v[i + 9] < 0.0f || m.v[i + 9] > 3.0f + 1e-3f) {
                printf("MESH NORM/AO FAIL\n");
                bad = 1;
            }
        }
        printf("mesh: %zu verts (%zu tris) %s\n", m.n / 12, m.n / 36, bad ? "FAIL" : "OK");
        if (bad) ok = 0;
        vox_mesh_free(&m);
    }
    return ok ? 0 : 1;
}
