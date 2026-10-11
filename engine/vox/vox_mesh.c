// Greedy-мешер: их World::buildChunk 1:1, локальные координаты чанка.
// Оси как у них: axis0: u=z,v=y; axis1: u=x,v=z; axis2: u=x,v=y.
// Вершина 12 float: pos3 norm3 uv2 tile ao day night. AO сырое 0..3
// (на /3 делит сам шейдер). model-матрица ядра добавляет смещение чанка.
#include "vox_mesh.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const VoxChunk *c;
    const VoxChunk *nb[6]; // -x,+x,-y,+y,-z,+z
} Ctx;

static uint8_t at(Ctx *g, int x, int y, int z) {
    if (y < 0 || y >= VOX_SY) return B_AIR;
    if (x >= 0 && x < VOX_SX && z >= 0 && z < VOX_SZ)
        return g->c->id[vox_idx(x, y, z)];
    int lx = x, lz = z;
    if (x < 0) lx = x + VOX_SX;
    else if (x >= VOX_SX) lx = x - VOX_SX;
    else if (z < 0) lz = z + VOX_SZ;
    else if (z >= VOX_SZ) lz = z - VOX_SZ;
    // Угол: обе оси OOB — диагонального соседа нет, воздух (не OOB-read).
    if (lx < 0 || lx >= VOX_SX || lz < 0 || lz >= VOX_SZ) return B_AIR;
    const VoxChunk *n = 0;
    if (x < 0) n = g->nb[0];
    else if (x >= VOX_SX) n = g->nb[1];
    else if (z < 0) n = g->nb[4];
    else n = g->nb[5];
    if (!n) return B_AIR;
    return n->id[vox_idx(lx, y, lz)];
}

static int occ(Ctx *g, int x, int y, int z) {
    return at(g, x, y, z) ? 1 : 0; // как у них: любой не-воздух (и вода) затеняет
}

static void push(VoxMeshOut *o, float v) {
    if (o->n + 1 > o->cap) {
        o->cap = o->cap ? o->cap * 2 : 4096;
        o->v = (float *)realloc(o->v, o->cap * sizeof(float));
    }
    o->v[o->n++] = v;
}

// Угол слитого квада. Позиция локальная, UV как у них:
// axis0: (worldZ, localY); axis1: (worldX, worldZ); axis2: (worldX, localY).
static void vert(VoxMeshOut *o, int axis, int sign, int s,
                 int u, int v, int du, int dv, float ao, int tile,
                 int ox, int oz) {
    float x, y, z, uu, vv, n[3] = {0, 0, 0};
    n[axis] = (float)sign;
    float plane = (float)(s + (sign > 0 ? 1 : 0));
    if (axis == 0) {
        x = plane; y = (float)(v + dv); z = (float)(u + du);
        uu = (float)(oz + u + du); vv = (float)(v + dv);
    } else if (axis == 1) {
        x = (float)(u + du); y = plane; z = (float)(v + dv);
        uu = (float)(ox + u + du); vv = (float)(oz + v + dv);
    } else {
        x = (float)(u + du); y = (float)(v + dv); z = plane;
        uu = (float)(ox + u + du); vv = (float)(v + dv);
    }
    push(o, x); push(o, y); push(o, z);
    push(o, n[0]); push(o, n[1]); push(o, n[2]);
    push(o, uu); push(o, vv);
    push(o, (float)tile);
    push(o, ao);
    push(o, 15.0f);
    push(o, 0.0f);
}

// AO угла (cu+du, cv+dv): 3 клетки снаружи грани. Топы плоские (=3).
static float cornerAO(Ctx *g, int axis, int sign, int s, int u, int v, int du, int dv) {
    if (axis == 1) return 3.0f;
    int out = s + (sign > 0 ? 1 : -1);
    int a0 = du ? 0 : -1, b0 = dv ? 0 : -1;
    int s1, s2, cc;
    if (axis == 0) {
        s1 = occ(g, out, v + dv, u + du + a0);
        s2 = occ(g, out, v + dv + b0, u + du);
        cc = occ(g, out, v + dv + b0, u + du + a0);
    } else {
        s1 = occ(g, u + du + a0, v + dv, out);
        s2 = occ(g, u + du, v + dv + b0, out);
        cc = occ(g, u + du + a0, v + dv + b0, out);
    }
    return (s1 && s2) ? 0.0f : (float)(3 - (s1 + s2 + cc));
}

void vox_mesh_build(const VoxChunk *c, const VoxChunk *nb[6], VoxMeshOut *out) {
    Ctx g;
    g.c = c;
    for (int i = 0; i < 6; i++) g.nb[i] = nb[i];
    int ox = c->cx * VOX_SX, oz = c->cz * VOX_SZ;
    for (int axis = 0; axis < 3; axis++) {
        int NU = VOX_SX, NV = (axis == 1) ? VOX_SZ : VOX_SY;
        int ns = (axis == 1) ? VOX_SY : VOX_SX;
        for (int di = 0; di < 2; di++) {
            int sign = di == 0 ? 1 : -1; // их порядок d: +1 потом -1
            for (int s = 0; s < ns; s++) {
                static uint8_t mId[64][16];
                static uint8_t mAo[64][16][4];
                static uint8_t mDone[64][16];
                for (int v = 0; v < NV; v++) {
                    for (int u = 0; u < NU; u++) {
                        int bx, by, bz, off = sign;
                        if (axis == 0) { bx = s; by = v; bz = u; }
                        else if (axis == 1) { bx = u; by = s; bz = v; }
                        else { bx = u; by = v; bz = s; }
                        int nx = axis == 0 ? off : 0;
                        int ny = axis == 1 ? off : 0;
                        int nz = axis == 2 ? off : 0;
                        uint8_t id = at(&g, bx, by, bz);
                        uint8_t ob = at(&g, bx + nx, by + ny, bz + nz);
                        int oOpaque = vox_opaque(ob) && !(vox_cutout(ob) && ob != id);
                        mDone[v][u] = 1;
                        mId[v][u] = 0;
                        if (!vox_solid(id) || oOpaque) continue;
                        mDone[v][u] = 0;
                        mId[v][u] = id;
                        mAo[v][u][0] = (uint8_t)cornerAO(&g, axis, sign, s, u, v, 0, 0);
                        mAo[v][u][1] = (uint8_t)cornerAO(&g, axis, sign, s, u, v, 1, 0);
                        mAo[v][u][2] = (uint8_t)cornerAO(&g, axis, sign, s, u, v, 1, 1);
                        mAo[v][u][3] = (uint8_t)cornerAO(&g, axis, sign, s, u, v, 0, 1);
                    }
                }
                for (int v = 0; v < NV; v++) {
                    for (int u = 0; u < NU; u++) {
                        if (mDone[v][u] || !mId[v][u]) continue;
                        int w = 1;
                        while (u + w < NU && !mDone[v][u + w] && mId[v][u + w] == mId[v][u] &&
                               !memcmp(mAo[v][u], mAo[v][u + w], 4)) w++;
                        int h = 1, grow = 1;
                        while (v + h < NV && grow) {
                            for (int k = 0; k < w; k++)
                                if (mDone[v + h][u + k] || mId[v + h][u + k] != mId[v][u] ||
                                    memcmp(mAo[v][u], mAo[v + h][u + k], 4)) { grow = 0; break; }
                            if (grow) h++;
                        }
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++) mDone[v + y][u + x] = 1;
                        // Углы слитого квада — свежие пробы (как у них).
                        float a00 = cornerAO(&g, axis, sign, s, u, v, 0, 0);
                        float a10 = cornerAO(&g, axis, sign, s, u, v, w, 0);
                        float a11 = cornerAO(&g, axis, sign, s, u, v, w, h);
                        float a01 = cornerAO(&g, axis, sign, s, u, v, 0, h);
                        int flip = (a00 + a11 > a01 + a10);
                        int tile = vox_tile(mId[v][u], axis, sign);
                        struct { int du, dv; float ao; } cr[4] =
                            {{0, 0, a00}, {w, 0, a10}, {w, h, a11}, {0, h, a01}};
                        // Их таблицы 1:1 (winding зависит от оси).
                        static const int T[2][2][2][6] = {
                            { // axis 0,1
                                {{0, 2, 1, 0, 3, 2}, {1, 3, 2, 1, 0, 3}}, // sign>0: flip0, flip1
                                {{0, 1, 2, 0, 2, 3}, {1, 2, 3, 1, 3, 0}}, // sign<0
                            },
                            { // axis 2
                                {{0, 1, 2, 0, 2, 3}, {1, 2, 3, 1, 3, 0}}, // sign>0
                                {{0, 2, 1, 0, 3, 2}, {1, 3, 2, 1, 0, 3}}, // sign<0
                            },
                        };
                        const int *t = T[axis == 2 ? 1 : 0][sign > 0 ? 0 : 1][flip ? 1 : 0];
                        for (int k = 0; k < 6; k++) {
                            int ci = t[k];
                            vert(out, axis, sign, s, u, v,
                                 cr[ci].du, cr[ci].dv, cr[ci].ao, tile, ox, oz);
                        }
                    }
                }
            }
        }
    }
}

void vox_mesh_free(VoxMeshOut *out) {
    free(out->v);
    out->v = 0;
    out->n = out->cap = 0;
}
