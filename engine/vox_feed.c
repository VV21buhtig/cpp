// Воксельный фидер: мешинг + слоты + модели. C11, static-состояние.
#include <string.h>
#include "vox_feed.h"
#include "vox/vox_mesh.h"
#include "mat4.h"

typedef struct {
    int used;
    int cx, cz;
} FeedSlot;

static FeedSlot slots[VOX_FEED_SLOTS];

int vox_feed_one(RenderCore *rc, const VoxChunk *chunk, const VoxChunk *nb[6]) {
    if (!rc || !rc->upload_voxmesh || !chunk) return -1;
    int slot = -1;
    for (int i = 0; i < VOX_FEED_SLOTS; i++)
        if (slots[i].used && slots[i].cx == chunk->cx && slots[i].cz == chunk->cz) {
            slot = i;
            break;
        }
    if (slot < 0)
        for (int i = 0; i < VOX_FEED_SLOTS; i++)
            if (!slots[i].used) { slot = i; break; }
    if (slot < 0) return -1; // пул полон
    VoxMeshOut m = {0, 0, 0};
    vox_mesh_build(chunk, nb, &m);
    int ok = 0;
    if (!m.n) {
        // Пустой чанк: гасим слот если был.
        if (slots[slot].used && rc->unload_voxmesh) rc->unload_voxmesh(rc, slot);
        slots[slot].used = 0;
        ok = 1;
    } else {
        Mat4 model = m4translate((float)(chunk->cx * VOX_SX), 0.0f,
                                 (float)(chunk->cz * VOX_SZ));
        ok = rc->upload_voxmesh(rc, slot, m.v, (int)(m.n / 12), model.m);
        if (ok) {
            slots[slot].used = 1;
            slots[slot].cx = chunk->cx;
            slots[slot].cz = chunk->cz;
        }
    }
    vox_mesh_free(&m);
    return ok ? slot : -1;
}

void vox_feed_drop(RenderCore *rc, int cx, int cz) {
    if (!rc || !rc->unload_voxmesh) return;
    for (int i = 0; i < VOX_FEED_SLOTS; i++)
        if (slots[i].used && slots[i].cx == cx && slots[i].cz == cz) {
            rc->unload_voxmesh(rc, i);
            slots[i].used = 0;
            return;
        }
}
