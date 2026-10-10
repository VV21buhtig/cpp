// Тест стриминга: игрок идёт по прямой 40 чанков, бюджет держится,
// кольцо полное, дальние выселяются.
#include <stdio.h>
#include "vox/vox_world.h"

int main(void) {
    static VoxWorld w;
    vox_world_init(&w, 1337);
    int maxGen = 0, fail = 0;
    // Игрок: чанк за 3 кадра (12 u/s при 4 u/s — с запасом), ensure каждый кадр.
    for (int step = 0; step < 12; step++) {
        for (int k = 0; k < 3; k++) {
            int g = vox_world_ensure(&w, step, 0);
            if (g > VOX_GEN_BUDGET) { printf("BUDGET FAIL: %d\n", g); fail = 1; }
            if (g > maxGen) maxGen = g;
        }
    }
    // Кольцо вокруг финала (11,0) полное: 9x9=81.
    int missing = 0;
    for (int dz = -VOX_RADIUS; dz <= VOX_RADIUS; dz++)
        for (int dx = -VOX_RADIUS; dx <= VOX_RADIUS; dx++)
            if (!vox_world_find(&w, 11 + dx, dz)) missing++;
    if (missing) { printf("HOLES: %d\n", missing); fail = 1; }
    printf("ring: missing=%d count=%d maxGen=%d %s\n", missing, w.count, maxGen, fail ? "FAIL" : "OK");
    // Выгрузка: уйти далеко — старые слоты свободны (финал 11 -> 40).
    for (int step = 12; step < 40; step++)
        for (int k = 0; k < 3; k++) vox_world_ensure(&w, step, 0);
    int leaked = 0;
    for (int i = 0; i < VOX_POOL; i++)
        if (w.slots[i].used && w.slots[i].cx < 39 - (VOX_RADIUS + 1)) leaked++;
    printf("evict: leaked=%d %s\n", leaked, leaked ? "FAIL" : "OK");
    return fail || leaked ? 1 : 0;
}
