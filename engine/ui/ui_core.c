// Фаза 1: минимальное retained-дерево (3 прямоугольника).
// Клик по виджету переключает его цвет = hit-test и state живы.
// Ничего кроме rect: текст/круги/линии — следующие фазы, шов уже готов.
#include <stdlib.h>
#include <string.h>
#include "ui.h"

#define UI_NWID 3

typedef struct {
    float x, y, w, h;
    float base[4]; // цвет покоя
    float alt[4]; // цвет по клику
    int on; // переключён
    int hover; // курсор внутри (подсветка рамкой? нет рамки в Фазе 1: ярче)
} UiWid;

struct EngUi {
    UiWid wids[UI_NWID];
    int dirty;
    UiFrame frame;
};

static int inside(const UiWid *w, float x, float y) {
    return x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h;
}

EngUi *eng_ui_create(void) {
    EngUi *ui = (EngUi *)calloc(1, sizeof *ui);
    if (!ui) return 0;
    // Три виджета лесенкой (как будущие панели): серый/синий/зелёный.
    static const float geom[UI_NWID][4] = {
        {60, 60, 220, 90}, {110, 190, 220, 90}, {160, 320, 220, 90},
    };
    static const float cols[UI_NWID][4] = {
        {0.35f, 0.35f, 0.38f, 1.0f},
        {0.10f, 0.39f, 0.61f, 1.0f},
        {0.13f, 0.55f, 0.13f, 1.0f},
    };
    for (int i = 0; i < UI_NWID; i++) {
        memcpy(ui->wids[i].base, cols[i], sizeof cols[i]);
        ui->wids[i].alt[0] = 1.0f;
        ui->wids[i].alt[1] = 0.65f;
        ui->wids[i].alt[2] = 0.10f;
        ui->wids[i].alt[3] = 1.0f;
        ui->wids[i].x = geom[i][0];
        ui->wids[i].y = geom[i][1];
        ui->wids[i].w = geom[i][2];
        ui->wids[i].h = geom[i][3];
    }
    ui->dirty = 1;
    return ui;
}

void eng_ui_destroy(EngUi *ui) {
    free(ui);
}

int eng_ui_event(EngUi *ui, const UiEvent *e) {
    if (!ui || !e) return 0;
    if (e->kind == UI_EV_MOVE) {
        int ch = 0;
        for (int i = 0; i < UI_NWID; i++) {
            int h = inside(&ui->wids[i], e->x, e->y);
            if (h != ui->wids[i].hover) {
                ui->wids[i].hover = h;
                ch = 1;
            }
        }
        if (ch) ui->dirty = 1;
        return ch;
    }
    if (e->kind == UI_EV_DOWN && e->button == 0) {
        // Hit-test сверху вниз (поздний виджет = верхний).
        for (int i = UI_NWID - 1; i >= 0; i--) {
            if (inside(&ui->wids[i], e->x, e->y)) {
                ui->wids[i].on = !ui->wids[i].on;
                ui->dirty = 1;
                return 1;
            }
        }
    }
    return 0;
}

int eng_ui_build(EngUi *ui, int w, int h, const UiFrame **out) {
    (void)w;
    (void)h;
    if (!ui || !out) return 0;
    if (ui->dirty) {
        // Y vec-ядра: начало снизу? RcVecShape rect x,y,w,h — как подадим,
        // так и ляжет (ориентацию проверит скрин Фазы 1, не гадаем).
        ui->frame.n = 0;
        for (int i = 0; i < UI_NWID && ui->frame.n < UI_MAX_PRIMS; i++) {
            UiWid *wd = &ui->wids[i];
            UiPrim *p = &ui->frame.prims[ui->frame.n++];
            const float *c = wd->on ? wd->alt : wd->base;
            float k = wd->hover ? 1.25f : 1.0f; // hover = ярче
            p->kind = 0;
            p->a = wd->x;
            p->b = wd->y;
            p->c = wd->w;
            p->d = wd->h;
            p->rgba[0] = c[0] * k > 1.0f ? 1.0f : c[0] * k;
            p->rgba[1] = c[1] * k > 1.0f ? 1.0f : c[1] * k;
            p->rgba[2] = c[2] * k > 1.0f ? 1.0f : c[2] * k;
            p->rgba[3] = 1.0f;
        }
        ui->dirty = 0;
        *out = &ui->frame;
        return 1;
    }
    *out = &ui->frame;
    return 0;
}
