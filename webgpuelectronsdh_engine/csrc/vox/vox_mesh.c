// Greedy-мешер: их логика (маска id+AO4, мемcmp, flip), индексы C-style.
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
    const VoxChunk *n = 0;
    int lx = x, lz = z;
    if (x < 0) { n = g->nb[0]; lx = x + VOX_SX; }
    else if (x >= VOX_SX) { n = g->nb[1]; lx = x - VOX_SX; }
    else if (z < 0) { n = g->nb[4]; lz = z + VOX_SZ; }
    else if (z >= VOX_SZ) { n = g->nb[5]; lz = z - VOX_SZ; }
    if (!n) return B_AIR;
    return n->id[vox_idx(lx, y, lz)];
}

static void push(VoxMeshOut *o, float v) {
    if (o->n + 1 > o->cap) {
        o->cap = o->cap ? o->cap * 2 : 4096;
        o->v = (float *)realloc(o->v, o->cap * sizeof(float));
    }
    o->v[o->n++] = v;
}

// dir: 0=x,1=y,2=z. sign +1/-1. Плоскость: NU,NV оси, срез s.
static void quad(VoxMeshOut *o, int dir, int sign, int s,
                 int u0, int v0, int u1, int v1, uint8_t id, int tile,
                 const int ao[4], int flip, int ox, int oz) {
    // углы (u0,v0),(u1,v1) в координатах плоскости; нормаль/позиция — в мир чанка
    float nrm[3] = {0, 0, 0};
    nrm[dir] = (float)sign;
    // вершины угла: a=(u0,v0) b=(u1,v0) c=(u1,v1) d=(u0,v1); порядок зависит от flip/sign
    int qu[4] = {u0, u1, u1, u0};
    int qv[4] = {v0, v0, v1, v1};
    int order[6];
    if ((flip && sign > 0) || (!flip && sign < 0)) {
        int o2[6] = {0, 1, 2, 0, 2, 3};
        memcpy(order, o2, sizeof order);
    } else {
        int o2[6] = {1, 2, 3, 1, 3, 0};
        memcpy(order, o2, sizeof order);
    }
    for (int k = 0; k < 6; k++) {
        int q = order[k];
        int uu = qu[q], vv = qv[q];
        float p[3];
        p[dir] = (float)(s + (sign > 0 ? 1 : 0));
        int du = (dir + 1) % 3, dv = (dir + 2) % 3;
        p[du] = (float)uu;
        p[dv] = (float)vv;
        // мировые XZ для UV/тайлинга: чанк-офсет наружу (ox,oz)
        float wu = (float)(du == 0 ? uu + ox : (du == 2 ? uu + oz : uu));
        float wv = (float)(dv == 0 ? vv + ox : (dv == 2 ? vv + oz : vv));
        if (du == 1) wu = (float)uu;
        if (dv == 1) wv = (float)vv;
        push(o, p[0]); push(o, p[1]); push(o, p[2]);
        push(o, nrm[0]); push(o, nrm[1]); push(o, nrm[2]);
        push(o, wu); push(o, wv);
        push(o, (float)tile);
        push(o, (float)ao[q] / 3.0f);
        push(o, 15.0f);
        push(o, 0.0f);
    }
    (void)ox; (void)oz;
}

void vox_mesh_build(const VoxChunk *c, const VoxChunk *nb[6], VoxMeshOut *out) {
    Ctx g;
    g.c = c;
    for (int i = 0; i < 6; i++) g.nb[i] = nb[i];
    int ox = c->cx * VOX_SX, oz = c->cz * VOX_SZ;
    // 6 направлений: ось 0=x,1=y,2=z; sign +-1
    for (int dir = 0; dir < 3; dir++) {
        int nu = (dir + 1) % 3 == 0 ? VOX_SX : ((dir + 1) % 3 == 1 ? VOX_SY : VOX_SZ);
        int nv = (dir + 2) % 3 == 0 ? VOX_SX : ((dir + 2) % 3 == 1 ? VOX_SY : VOX_SZ);
        (void)nu; (void)nv;
        int au = (dir + 1) % 3, av = (dir + 2) % 3;
        int su = au == 0 ? VOX_SX : (au == 1 ? VOX_SY : VOX_SZ);
        int sv = av == 0 ? VOX_SX : (av == 1 ? VOX_SY : VOX_SZ);
        int ns = dir == 0 ? VOX_SX : (dir == 1 ? VOX_SY : VOX_SZ);
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int s = 0; s < ns; s++) {
                // маска [sv][su]: id + ao[4], done
                static uint8_t mId[64][64];
                static uint8_t mAo[64][64][4];
                static uint8_t mDone[64][64];
                for (int v = 0; v < sv; v++) {
                    for (int u = 0; u < su; u++) {
                        int pp[3] = {0, 0, 0}, bp[3] = {0, 0, 0};
                        pp[dir] = s; bp[dir] = s + (sign > 0 ? 1 : -1);
                        pp[au] = u; bp[au] = u;
                        pp[av] = v; bp[av] = v;
                        uint8_t id = at(&g, pp[0], pp[1], pp[2]);
                        uint8_t ob = at(&g, bp[0], bp[1], bp[2]);
                        int oOpaque = vox_opaque(ob) && !(vox_cutout(ob) && ob != id);
                        mDone[v][u] = 1;
                        mId[v][u] = 0;
                        if (!vox_solid(id) || oOpaque) continue;
                        mDone[v][u] = 0;
                        mId[v][u] = id;
                        // AO углов: 3 соседа со стороны грани
                        int ao[4];
                        int cu[4] = {u, u + 1, u, u + 1};
                        int cv[4] = {v, v, v + 1, v + 1};
                        for (int q = 0; q < 4; q++) {
                            int sp[3] = {0, 0, 0}, s1[3] = {0, 0, 0}, s2[3] = {0, 0, 0}, dg[3] = {0, 0, 0};
                            int quu = cu[q] - u, qvv = cv[q] - v; // 0/1
                            sp[dir] = s + (sign > 0 ? 1 : -1);
                            sp[au] = u + quu - (quu ? 0 : 0);
                            sp[av] = v + qvv;
                            // углы: сторона1 вдоль u, сторона2 вдоль v, диаг
                            s1[dir] = s + (sign > 0 ? 1 : -1);
                            s1[au] = u + (quu ? 1 : -1);
                            s1[av] = v + qvv;
                            s2[dir] = s + (sign > 0 ? 1 : -1);
                            s2[au] = u + quu;
                            s2[av] = v + (qvv ? 1 : -1);
                            dg[dir] = s + (sign > 0 ? 1 : -1);
                            dg[au] = u + (quu ? 1 : -1);
                            dg[av] = v + (qvv ? 1 : -1);
                            int o1 = at(&g, s1[0], s1[1], s1[2]) ? 1 : 0;
                            int o2 = at(&g, s2[0], s2[1], s2[2]) ? 1 : 0;
                            int od = at(&g, dg[0], dg[1], dg[2]) ? 1 : 0;
                            int a = (o1 && o2) ? 0 : 3 - (o1 + o2 + od);
                            if (dir == 1) a = 3;
                            ao[q] = a;
                        }
                        mAo[v][u][0] = (uint8_t)ao[0];
                        mAo[v][u][1] = (uint8_t)ao[1];
                        mAo[v][u][2] = (uint8_t)ao[2];
                        mAo[v][u][3] = (uint8_t)ao[3];
                    }
                }
                // greedy-мерж по маске
                for (int v = 0; v < sv; v++) {
                    for (int u = 0; u < su; u++) {
                        if (mDone[v][u] || !mId[v][u]) continue;
                        int w = 1;
                        while (u + w < su && !mDone[v][u + w] && mId[v][u + w] == mId[v][u] &&
                               !memcmp(mAo[v][u], mAo[v][u + w], 4)) w++;
                        int h = 1;
                        int ok = 1;
                        while (v + h < sv && ok) {
                            for (int k = 0; k < w; k++)
                                if (mDone[v + h][u + k] || mId[v + h][u + k] != mId[v][u] ||
                                    memcmp(mAo[v][u], mAo[v + h][u + k], 4)) { ok = 0; break; }
                            if (ok) h++;
                        }
                        for (int y = 0; y < h; y++)
                            for (int x = 0; x < w; x++) mDone[v + y][u + x] = 1;
                        int ao4[4] = {mAo[v][u][0], mAo[v + h - 1][u][1],
                                      mAo[v + h - 1][u + w - 1][2], mAo[v][u + w - 1][3]};
                        int flip = (ao4[0] + ao4[2] > ao4[1] + ao4[3]);
                        quad(out, dir, sign, s, u, v, u + w, v + h,
                             mId[v][u], vox_tile(mId[v][u], dir, sign), ao4, flip, ox, oz);
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
