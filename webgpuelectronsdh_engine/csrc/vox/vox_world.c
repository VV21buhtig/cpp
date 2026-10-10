#include "vox_world.h"
#include "vox_gen.h"

void vox_world_init(VoxWorld *w, uint32_t seed) {
    for (int i = 0; i < VOX_POOL; i++) w->slots[i].used = 0;
    w->count = 0;
    w->seed = seed;
    w->genLast = 0;
}

VoxChunk *vox_world_find(VoxWorld *w, int cx, int cz) {
    for (int i = 0; i < VOX_POOL; i++)
        if (w->slots[i].used && w->slots[i].cx == cx && w->slots[i].cz == cz)
            return &w->slots[i].data;
    return 0;
}

static VoxSlot *free_slot(VoxWorld *w) {
    for (int i = 0; i < VOX_POOL; i++)
        if (!w->slots[i].used) return &w->slots[i];
    return 0;
}

static int cheb(int dx, int dz) {
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx > dz ? dx : dz;
}

int vox_world_ensure(VoxWorld *w, int pcx, int pcz) {
    // Выселение за кольцом R+1.
    for (int i = 0; i < VOX_POOL; i++) {
        if (!w->slots[i].used) continue;
        if (cheb(w->slots[i].cx - pcx, w->slots[i].cz - pcz) > VOX_RADIUS + 1) {
            w->slots[i].used = 0;
            w->count--;
        }
    }
    // Догрузка кольцами от ближнего: бюджет режет хвост.
    int gen = 0;
    for (int r = 0; r <= VOX_RADIUS && gen < VOX_GEN_BUDGET; r++) {
        for (int dz = -r; dz <= r && gen < VOX_GEN_BUDGET; dz++) {
            for (int dx = -r; dx <= r && gen < VOX_GEN_BUDGET; dx++) {
                if (dx != r && dx != -r && dz != r && dz != -r) continue; // только обод
                int cx = pcx + dx, cz = pcz + dz;
                if (vox_world_find(w, cx, cz)) continue;
                VoxSlot *s = free_slot(w);
                if (!s) return gen; // пул полон — ждём выселения в след. кадре
                s->used = 1;
                s->cx = cx;
                s->cz = cz;
                s->dirty = 1;
                s->data.cx = cx;
                s->data.cz = cz;
                vox_gen(&s->data, w->seed);
                w->count++;
                gen++;
            }
        }
    }
    w->genLast = gen;
    return gen;
}
