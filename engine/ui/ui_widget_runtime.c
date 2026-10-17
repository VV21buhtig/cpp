// §14 F3c: общий рантайм виджетов (см. ui_widget_runtime.h).
// Портировано дословно из редактора (ui_edit.c), чтобы игра и редактор
// рисовали/хитили ОДНИМ кодом: порядок слоёв, пропуск прозрачных, верхний хит.
#include "ui_widget_runtime.h"

#include <string.h>

int ui_widget_sib_sorted(const UiWidgetTree *t, int parent, int *out) {
    if (!t || !out) return 0;
    int n = 0;
    for (int i = 0; i < UI_MAX_WIDGETS; i++)
        if (t->alive[i] && t->items[i].parent == parent) out[n++] = i + 1;
    // Вставками: (z, id) возрастает -> поздние рисуются/хитятся выше.
    for (int i = 1; i < n; i++) {
        int k = out[i], j = i - 1;
        while (j >= 0) {
            const UiWidget *a = widget_cget(t, out[j]);
            const UiWidget *b = widget_cget(t, k);
            if (a->z > b->z || (a->z == b->z && out[j] > k)) {
                out[j + 1] = out[j];
                j--;
            } else
                break;
        }
        out[j + 1] = k;
    }
    return n;
}

static void emit_rect(RcVecShape *sh, int cap, int *n, float x, float y, float w,
                      float h, const float rgba[4], float round) {
    if (*n >= cap) return;
    RcVecShape *s = &sh[(*n)++];
    s->kind = 0;
    s->a = x;
    s->b = y;
    s->c = w;
    s->d = h;
    s->e = round;
    s->f = 0;
    memcpy(s->rgba, rgba, sizeof s->rgba);
}

static void build_node(const UiWidgetTree *t, int W, int H, RcVecShape *sh, int cap,
                       int *n, int id) {
    const UiWidget *w = widget_cget(t, id);
    if (!w) return;
    float x, y, ww, hh;
    widget_rect_px(w, W, H, &x, &y, &ww, &hh);
    float col[4] = {w->style.bg[0], w->style.bg[1], w->style.bg[2],
                    w->style.bg[3] * w->style.alpha};
    if (col[3] > 0.001f) emit_rect(sh, cap, n, x, y, ww, hh, col, w->style.rounding);
    int kids[UI_MAX_WIDGETS];
    int nk = ui_widget_sib_sorted(t, id, kids); // дети — по слоям
    for (int i = 0; i < nk; i++) build_node(t, W, H, sh, cap, n, kids[i]);
}

int ui_widget_build(const UiWidgetTree *t, int W, int H, RcVecShape *sh, int cap) {
    if (!t || !sh || cap <= 0) return 0;
    int n = 0;
    int roots[UI_MAX_WIDGETS];
    int nr = ui_widget_sib_sorted(t, 0, roots);
    for (int i = 0; i < nr; i++) build_node(t, W, H, sh, cap, &n, roots[i]);
    return n;
}

// Обход в ТОМ ЖЕ порядке, что отрисовка: последний попавший = верхний.
static void hit_node(const UiWidgetTree *t, int W, int H, float px, float py, int id,
                     int *best) {
    const UiWidget *w = widget_cget(t, id);
    if (!w) return;
    float x, y, ww, hh;
    widget_rect_px(w, W, H, &x, &y, &ww, &hh);
    if (px >= x && px < x + ww && py >= y && py < y + hh) *best = id;
    int kids[UI_MAX_WIDGETS];
    int nk = ui_widget_sib_sorted(t, id, kids);
    for (int i = 0; i < nk; i++) hit_node(t, W, H, px, py, kids[i], best);
}

int ui_widget_hit(const UiWidgetTree *t, int W, int H, float px, float py) {
    if (!t) return 0;
    int best = 0;
    int roots[UI_MAX_WIDGETS];
    int nr = ui_widget_sib_sorted(t, 0, roots);
    for (int i = 0; i < nr; i++) hit_node(t, W, H, px, py, roots[i], &best);
    return best;
}
