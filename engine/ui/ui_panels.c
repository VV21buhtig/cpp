// Раскладка 1280x720 (одна тема, без светло/тёмно-возни):
// менюбар y0-26 | тулбар y26-58 | иерархия x0-250 y58-720 | статус y694-720.
// Цвета: фон панелей тёмный, шапки светлее, select синий, tool активный оранжевый.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_panels.h"

#define PN_MAXROWS 64

typedef struct {
    float x, y, w, h;
    unsigned id; // entity или 0
    int depth;
} PnRow;

struct UiPanels {
    unsigned selected;
    int tool;
    int dirty;
    UiFrame frame;
    char flash[64]; // последняя команда меню (File stub и т.п.)
    PnRow rows[PN_MAXROWS];
    int nrows;
};

static const float C_BG[4] = {0.16f, 0.17f, 0.19f, 1.0f};
static const float C_BAR[4] = {0.23f, 0.24f, 0.27f, 1.0f};
static const float C_SEL[4] = {0.10f, 0.36f, 0.60f, 1.0f};
static const float C_TOOL[4] = {0.85f, 0.50f, 0.10f, 1.0f};
static const float C_TXT[4] = {0.85f, 0.85f, 0.87f, 1.0f};
static const float C_DIM[4] = {0.55f, 0.55f, 0.58f, 1.0f};

UiPanels *ui_panels_create(void) {
    UiPanels *p = (UiPanels *)calloc(1, sizeof *p);
    if (!p) return 0;
    p->dirty = 1;
    return p;
}

void ui_panels_destroy(UiPanels *p) {
    free(p);
}

static void emit(UiPanels *p, float x, float y, float w, float h, const float *c) {
    if (p->frame.n >= UI_MAX_PRIMS) return;
    UiPrim *q = &p->frame.prims[p->frame.n++];
    q->kind = 0;
    q->a = x;
    q->b = y;
    q->c = w;
    q->d = h;
    memcpy(q->rgba, c, sizeof q->rgba);
}

static int depth_of(ScWorld *s, Entity e) {
    int d = 0;
    Entity cur = e;
    while (d < 4) {
        Entity par = sc_parent_of(s, cur);
        if (!par) break;
        d++;
        cur = par;
    }
    return d;
}

int ui_panels_event(UiPanels *p, ScWorld *scene, const UiEvent *e) {
    if (!p || !e) return 0;
    if (e->kind == UI_EV_KEY) {
        int t = -1;
        if (e->key == 'Q') t = 0;
        else if (e->key == 'W') t = 1;
        else if (e->key == 'E') t = 2;
        else if (e->key == 'R') t = 3;
        if (t >= 0 && t != p->tool) {
            p->tool = t;
            p->dirty = 1;
            return 1;
        }
        return 0;
    }
    if (e->kind != UI_EV_DOWN || e->button != 0) return 0;
    float x = e->x, y = e->y;
    // Тулбар: 4 кнопки по 64px с x=8.
    if (y >= 26 && y < 58 && x >= 8 && x < 8 + 4 * 64) {
        int t = (int)((x - 8) / 64);
        if (t != p->tool) {
            p->tool = t;
            p->dirty = 1;
            return 1;
        }
        return 0;
    }
    // Менюбар: File|Edit|View — заглушки с flash (команды позже).
    if (y >= 0 && y < 26 && x >= 8 && x < 200) {
        const char *m = x < 70 ? "File" : (x < 135 ? "Edit" : "View");
        snprintf(p->flash, sizeof p->flash, "%s (stub)", m);
        p->dirty = 1;
        return 1;
    }
    // Иерархия: строки.
    for (int i = 0; i < p->nrows; i++) {
        if (x >= p->rows[i].x && x < p->rows[i].x + p->rows[i].w && y >= p->rows[i].y &&
            y < p->rows[i].y + p->rows[i].h) {
            if (p->selected != p->rows[i].id) {
                p->selected = p->rows[i].id;
                p->dirty = 1;
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

int ui_panels_build(UiPanels *p, ScWorld *scene, int w, int h, const UiFrame **out) {
    if (!p || !out) return 0;
    if (!p->dirty) {
        *out = &p->frame;
        return 0;
    }
    p->frame.n = 0;
    // Менюбар + тулбар.
    emit(p, 0, 0, (float)w, 26, C_BAR);
    emit(p, 0, 26, (float)w, 32, C_BG);
    for (int i = 0; i < 4; i++)
        if (i == p->tool) emit(p, 8 + (float)(i * 64), 28, 60, 28, C_TOOL);
    // Иерархия: фон + строки.
    emit(p, 0, 58, 250, (float)(h - 58), C_BG);
    p->nrows = 0;
    if (scene) {
        Entity list[SC_MAX];
        int n = sc_list(scene, list, SC_MAX);
        float y = 64;
        for (int i = 0; i < n && p->nrows < PN_MAXROWS && y + 24 < h; i++) {
            int d = depth_of(scene, list[i]);
            float rx = 8 + (float)(d * 16);
            if (list[i] == p->selected)
                emit(p, 4, y, 242, 24, C_SEL);
            if (p->nrows < PN_MAXROWS) {
                p->rows[p->nrows].x = rx;
                p->rows[p->nrows].y = y;
                p->rows[p->nrows].w = 250 - rx;
                p->rows[p->nrows].h = 24;
                p->rows[p->nrows].id = list[i];
                p->rows[p->nrows].depth = d;
                p->nrows++;
            }
            y += 24;
        }
    }
    // Статусбар.
    emit(p, 250, (float)(h - 26), (float)(w - 250), 26, C_BAR);
    p->dirty = 0;
    *out = &p->frame;
    return 1;
}

void ui_panels_text(UiPanels *p, ScWorld *scene, UiText *text, int w, int h) {
    (void)w;
    (void)h;
    if (!p || !text) return;
    ui_text_quad(text, 12, 4, "File  Edit  View", C_DIM);
    if (p->flash[0]) ui_text_quad(text, 220, 4, p->flash, C_DIM);
    static const char *tools[4] = {"Q:Sel", "W:Move", "E:Rot", "R:Scale"};
    for (int i = 0; i < 4; i++)
        ui_text_quad(text, 14 + (float)(i * 64), 32, tools[i], i == p->tool ? C_TOOL : C_TXT);
    for (int i = 0; i < p->nrows; i++) {
        const char *nm = scene ? sc_name(scene, p->rows[i].id) : "?";
        char buf[80];
        snprintf(buf, sizeof buf, "%s", nm && nm[0] ? nm : "?");
        ui_text_quad(text, p->rows[i].x + 4, p->rows[i].y + 4, buf, C_TXT);
    }
    // Статус: selected + tool.
    char st[128];
    const char *sel = "?";
    if (scene && p->selected) {
        const char *nm = sc_name(scene, p->selected);
        if (nm && nm[0]) sel = nm;
    }
    snprintf(st, sizeof st, "sel: %s | tool: %d", sel, p->tool);
    ui_text_quad(text, 260, (float)(h - 22), st, C_DIM);
}

unsigned ui_panels_selected(const UiPanels *p) {
    return p ? p->selected : 0;
}

int ui_panels_tool(const UiPanels *p) {
    return p ? p->tool : 0;
}
