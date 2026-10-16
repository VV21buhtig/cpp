// C-Node Graph: eval топологическим порядком (Kahn). Цикл = отказ.
// Malloc: создание/рост пулов (не кадр — eval без аллокаций).
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "graph.h"

#define GR_MAXN 256
#define GR_MAXL 256

typedef struct {
    int id; // >0, стабилен (пережил save/load)
    int type;
    float p0, p1;
    float val; // значение после eval
} GrNode;

typedef struct {
    int fn, fp, tn, tp;
} GrLink;

struct EngGraph {
    GrNode nodes[GR_MAXN];
    int nn;
    int nextId;
    GrLink links[GR_MAXL];
    int nl;
};

EngGraph *eng_graph_create(void) {
    EngGraph *g = (EngGraph *)calloc(1, sizeof *g);
    if (g) g->nextId = 1;
    return g;
}

void eng_graph_destroy(EngGraph *g) {
    free(g);
}

// Порты типа: входы/выходы. 1 есть, 0 нет.
static int has_in(int type, int port) {
    if (port < 0) return 0;
    if (type == GN_ADD || type == GN_MUL) return port < 2;
    if (type == GN_SIN || type == GN_OUT) return port == 0;
    return 0;
}

static int has_out(int type, int port) {
    if (port != 0) return 0;
    return type == GN_CONST || type == GN_TIME || type == GN_ADD || type == GN_MUL ||
           type == GN_SIN;
}

static GrNode *gr_find(EngGraph *g, int id) {
    for (int i = 0; i < g->nn; i++)
        if (g->nodes[i].id == id) return &g->nodes[i];
    return 0;
}

int eng_graph_add_node(EngGraph *g, int type, float p0, float p1) {
    if (!g || g->nn >= GR_MAXN) return 0;
    if (type != GN_CONST && type != GN_TIME && type != GN_ADD && type != GN_MUL &&
        type != GN_SIN && type != GN_OUT)
        return 0;
    if (type == GN_OUT && (p0 < 0 || p0 != (float)(int)p0 || p0 > 63)) return 0;
    (void)p1; // запас под второй параметр (пока не используется)
    GrNode *n = &g->nodes[g->nn++];
    n->id = g->nextId++;
    n->type = type;
    n->p0 = p0;
    n->p1 = p1;
    n->val = 0;
    return n->id;
}

int eng_graph_link(EngGraph *g, int fn, int fp, int tn, int tp) {
    if (!g || g->nl >= GR_MAXL) return 0;
    GrNode *a = gr_find(g, fn);
    GrNode *b = gr_find(g, tn);
    if (!a || !b || a == b) return 0;
    if (!has_out(a->type, fp) || !has_in(b->type, tp)) return 0;
    for (int i = 0; i < g->nl; i++) // один драйвер на вход
        if (g->links[i].tn == tn && g->links[i].tp == tp) return 0;
    g->links[g->nl].fn = fn;
    g->links[g->nl].fp = fp;
    g->links[g->nl].tn = tn;
    g->links[g->nl].tp = tp;
    g->nl++;
    return 1;
}

int eng_graph_nnodes(const EngGraph *g) {
    return g ? g->nn : 0;
}

int eng_graph_nlinks(const EngGraph *g) {
    return g ? g->nl : 0;
}

// Клон содержимого (load поверх: замена целиком, id сохраняются).
int eng_graph_clone(EngGraph *dst, const EngGraph *src) {
    if (!dst || !src) return 0;
    *dst = *src;
    return 1;
}

int eng_graph_node_at(const EngGraph *g, int idx, int *id, int *type, float *p0,
                      float *p1) {
    if (!g || idx < 0 || idx >= g->nn) return 0;
    if (id) *id = g->nodes[idx].id;
    if (type) *type = g->nodes[idx].type;
    if (p0) *p0 = g->nodes[idx].p0;
    if (p1) *p1 = g->nodes[idx].p1;
    return 1;
}

int eng_graph_link_at(const EngGraph *g, int idx, int *fn, int *fp, int *tn, int *tp) {
    if (!g || idx < 0 || idx >= g->nl) return 0;
    if (fn) *fn = g->links[idx].fn;
    if (fp) *fp = g->links[idx].fp;
    if (tn) *tn = g->links[idx].tn;
    if (tp) *tp = g->links[idx].tp;
    return 1;
}

// Вход узла: значение драйвера или 0 (несвязан).
static float gr_in(EngGraph *g, int id, int port) {
    for (int i = 0; i < g->nl; i++) {
        if (g->links[i].tn == id && g->links[i].tp == port) {
            GrNode *d = gr_find(g, g->links[i].fn);
            return d ? d->val : 0.0f;
        }
    }
    return 0.0f;
}

int eng_graph_eval(EngGraph *g, float t, float *out, int cap) {
    if (!g || !out || cap <= 0) return -1;
    // Kahn: входная степень = число связанных входов.
    int indeg[GR_MAXN];
    for (int i = 0; i < g->nn; i++) {
        int d = 0;
        GrNode *n = &g->nodes[i];
        for (int p = 0; has_in(n->type, p); p++)
            for (int k = 0; k < g->nl; k++)
                if (g->links[k].tn == n->id && g->links[k].tp == p) {
                    d++;
                    break;
                }
        indeg[i] = d;
    }
    int done[GR_MAXN];
    memset(done, 0, sizeof done);
    int maxSlot = -1;
    for (int step = 0; step < g->nn; step++) {
        int pick = -1;
        for (int i = 0; i < g->nn; i++)
            if (!done[i] && indeg[i] == 0) {
                pick = i;
                break;
            }
        if (pick < 0) return -1; // цикл: не всё разрешилось
        GrNode *n = &g->nodes[pick];
        switch (n->type) {
        case GN_CONST: n->val = n->p0; break;
        case GN_TIME: n->val = t; break;
        case GN_ADD: n->val = gr_in(g, n->id, 0) + gr_in(g, n->id, 1); break;
        case GN_MUL: n->val = gr_in(g, n->id, 0) * gr_in(g, n->id, 1); break;
        case GN_SIN: n->val = sinf(gr_in(g, n->id, 0)); break;
        case GN_OUT: {
            int s = (int)n->p0;
            if (s >= cap) return -1;
            out[s] = gr_in(g, n->id, 0);
            if (s > maxSlot) maxSlot = s;
            n->val = out[s];
            break;
        }
        default: return -1;
        }
        done[pick] = 1;
        // Снимаем зависимости выходящих рёбер.
        for (int k = 0; k < g->nl; k++) {
            if (g->links[k].fn != n->id) continue;
            for (int i = 0; i < g->nn; i++)
                if (!done[i] && g->nodes[i].id == g->links[k].tn) indeg[i]--;
        }
    }
    return maxSlot + 1; // 0 если OUT нет (maxSlot -1)
}
