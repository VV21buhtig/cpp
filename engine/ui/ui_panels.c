// Раскладка 1280x720 (одна тема, без светло/тёмно-возни):
// менюбар y0-26 | тулбар y26-58 | иерархия x0-250 y58-720 | статус y694-720.
// Цвета: фон панелей тёмный, шапки светлее, select синий, tool активный оранжевый.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_panels.h"
#include "ui_reflect.h" // дескрипторы Transform: один список на всех
#include "ui_theme.h" // одна тема + hot-reload по mtime
#include "ui_undo.h" // стек команд float-полей
#include "ui_bind.h" // UiReply/UiAttrF (FReply + TAttribute по-Slate)

#define PN_MAXROWS 64

typedef struct {
    float x, y, w, h;
    unsigned id; // entity или 0
    int depth;
} PnRow;

#define PN_NF 9 // полей трансформа в инспекторе (pos/rot/scale xyz)

typedef struct {
    float x, y, w, h;
    int field; // 0..8 индекс дескриптора
} PnField;

// Живая привязка строки инспектора к полю сущности (TAttribute<float>).
typedef struct {
    ScWorld *scene;
    Entity ent;
    int field; // индекс transform_fields
} UiEcsRef;

struct UiPanels {
    unsigned selected;
    int tool;
    int dirty;
    int posed; // трансформ сущности меняли (тесту надо перекачать суп)
    int showHier; // 1 панель иерархии (0 = полоска 28px, вьюпорту больше места)
    int showInsp; // 1 панель инспектора (0 = полоска)
    int hierW; // ширина иерархии (220) / полоски (28)
    int inspW; // ширина инспектора (220) / полоски (28)
    UiFrame frame;
    char flash[64]; // последняя команда меню (File stub и т.п.)
    PnRow rows[PN_MAXROWS];
    int nrows;
    PnField frows[PN_NF];
    int scrubField; // -1 нет модального скраба (захват мыши как CaptureMouse)
    float scrubWas; // значение на DOWN (для undo-коммита на UP)
    float scrubX0;
    UiTheme theme; // одна тема (hot-reload по mtime)
    UiUndo undo; // стек команд (Фаза 5)
    UiEcsRef erefs[PN_NF]; // привязки строк инспектора к ECS (TAttribute)
    UiAttrF binds[PN_NF];
    int winW, winH; // размер окна с последней сборки (для хит-боксов справа)
};

#define THEME_PATH "engine/assets/theme.json"


UiPanels *ui_panels_create(void) {
    UiPanels *p = (UiPanels *)calloc(1, sizeof *p);
    if (!p) return 0;
    p->dirty = 1;
    p->scrubField = -1;
    p->showHier = 1;
    p->showInsp = 1;
    p->hierW = 220;
    p->inspW = 220;
    ui_theme_dark(&p->theme);
    ui_theme_load(&p->theme, THEME_PATH); // нет файла — дефолт, не ошибка
    ui_undo_reset(&p->undo);
    return p;
}

// Прямоугольник вьюпорта (док-модель UE: остаток после панелей).
// Пишет x,y,w,h (y вниз). Вызывающий пересоздаёт ТОЛЬКО цель (не ядро/окно).
void ui_panels_viewport(const UiPanels *p, int W, int H, int *x, int *y, int *w, int *h) {
    int hx = p && p->showHier ? p->hierW : 0;
    int ix = p && p->showInsp ? p->inspW : 0;
    if (x) *x = hx;
    if (y) *y = 58;
    if (w) *w = W - hx - ix;
    if (h) *h = H - 58 - 26;
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

static float ecs_get(void *user) {
    UiEcsRef *r = (UiEcsRef *)user;
    if (!r || !r->scene || !sc_alive(r->scene, r->ent)) return 0.0f;
    ScTransform t = sc_get(r->scene, r->ent);
    return ui_field_get_f(&t, &transform_fields[r->field]);
}

static void ecs_set(void *user, float v) {
    UiEcsRef *r = (UiEcsRef *)user;
    if (!r || !r->scene || !sc_alive(r->scene, r->ent)) return;
    ScTransform t = sc_get(r->scene, r->ent);
    ui_field_set_f(&t, &transform_fields[r->field], v);
    sc_set(r->scene, r->ent, &t);
}

// Перепривязать 9 строк к сущности (при смене selection).
static void rebind(UiPanels *p, ScWorld *scene, unsigned ent) {
    for (int i = 0; i < PN_NF; i++) {
        p->erefs[i].scene = scene;
        p->erefs[i].ent = ent;
        p->erefs[i].field = i;
        p->binds[i].bound = 1;
        p->binds[i].value = 0.0f;
        p->binds[i].get = ecs_get;
        p->binds[i].set = ecs_set;
        p->binds[i].user = &p->erefs[i];
    }
}

UiReply ui_panels_event(UiPanels *p, ScWorld *scene, const UiEvent *e) {
    if (!p || !e) return ui_unhandled();
    // Модальный скраб: тянешь значение — живьём через бинд (коммит на UP).
    if (p->scrubField >= 0) {
        if (e->kind == UI_EV_MOVE) {
            float v = p->scrubWas + (e->x - p->scrubX0) * 0.05f;
            ui_attr_f_set(&p->binds[p->scrubField], v);
            p->posed = 1;
            p->dirty = 1;
            return ui_handled(1, 1); // держим захват до UP
        }
        if (e->kind == UI_EV_UP) {
            // Коммит mouse-up: {было -> стало} в undo (как Unity flush).
            float now = ui_attr_f_get(&p->binds[p->scrubField]);
            ui_undo_push(&p->undo, p->selected, p->scrubField, p->scrubWas, now);
            p->scrubField = -1;
            p->dirty = 1;
            return ui_handled(1, 0); // отпустили захват
        }
        return ui_unhandled();
    }
    if (e->kind == UI_EV_KEY) {
        // Tab: спрятать/вернуть обе панели (фулскрин вьюпорта, как UE F11).
        // GLFW_KEY_TAB=258 (не ASCII!). Моды: 1 shift 2 ctrl 4 alt.
        if (e->key == 258 && !(e->mods & 6)) {
            int any = p->showHier || p->showInsp;
            p->showHier = any ? 0 : 1;
            p->showInsp = any ? 0 : 1;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        // Ctrl+Z откат, Ctrl+Shift+Z / Ctrl+Y повтор (как везде).
        if ((e->mods & 2) && (e->key == 'Z' || e->key == 'Y')) {
            int isUndo = (e->key == 'Z' && !(e->mods & 1));
            UiCmd c;
            int ok = isUndo ? ui_undo_undo(&p->undo, &c) : ui_undo_redo(&p->undo, &c);
            if (ok && scene && sc_alive(scene, c.ent)) {
                rebind(p, scene, c.ent);
                ui_attr_f_set(&p->binds[c.field], isUndo ? c.before : c.after);
                p->posed = 1;
                p->dirty = 1;
                return ui_handled(1, 0);
            }
            return ui_unhandled();
        }
    }
    if (e->kind == UI_EV_KEY) {
        int t = -1;
        if (e->key == 'Q') t = 0;
        else if (e->key == 'W') t = 1;
        else if (e->key == 'E') t = 2;
        else if (e->key == 'R') t = 3;
        if (t >= 0 && t != p->tool) {
            p->tool = t;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    if (e->kind != UI_EV_DOWN || e->button != 0) return ui_unhandled();
    float x = e->x, y = e->y;
    int hx = p->showHier ? p->hierW : 0;
    int ix = p->showInsp ? p->inspW : 0;
    // Кнопки коллапса раньше контента (хром первее строк).
    // Закрытая панель = 0px (возврат через Tab/View, полосок нет).
    if (y >= 58 && y < 82) {
        if (p->showHier && x >= hx - 28 && x < hx) {
            p->showHier = 0;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        // w нужен для правой кнопки: берём из последней сборки через frows? нет —
        // ширина окна известна вызывающему; храним в панели при build.
        if (p->showInsp && x >= p->winW - ix && x < p->winW - ix + 28) {
            p->showInsp = 0;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
    }
    // Инспектор: DOWN на строке поля = начать скраб через бинд (коммит на UP).
    if (scene && p->selected && sc_alive(scene, p->selected)) {
        for (int i = 0; i < PN_NF; i++) {
            if (x >= p->frows[i].x && x < p->frows[i].x + p->frows[i].w &&
                y >= p->frows[i].y && y < p->frows[i].y + p->frows[i].h) {
                rebind(p, scene, p->selected);
                p->scrubField = p->frows[i].field;
                p->scrubWas = ui_attr_f_get(&p->binds[p->scrubField]);
                p->scrubX0 = x;
                return ui_handled(0, 1); // тихо + захват (грязь будет на MOVE)
            }
        }
    }
    // Тулбар: 4 кнопки по 64px с x=8.
    if (y >= 26 && y < 58 && x >= 8 && x < 8 + 4 * 64) {
        int t = (int)((x - 8) / 64);
        if (t != p->tool) {
            p->tool = t;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    // Менюбар: File|Edit — заглушки с flash; View — показать/скрыть панели.
    if (y >= 0 && y < 26 && x >= 8 && x < 200) {
        if (x >= 135) { // View
            int any = p->showHier || p->showInsp;
            p->showHier = any ? 0 : 1;
            p->showInsp = any ? 0 : 1;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        const char *m = x < 70 ? "File" : "Edit";
        snprintf(p->flash, sizeof p->flash, "%s (stub)", m);
        p->dirty = 1;
        return ui_handled(1, 0);
    }
    // Иерархия: строки (смена selection перепривязывает инспектор).
    for (int i = 0; i < p->nrows; i++) {
        if (x >= p->rows[i].x && x < p->rows[i].x + p->rows[i].w && y >= p->rows[i].y &&
            y < p->rows[i].y + p->rows[i].h) {
            if (p->selected != p->rows[i].id) {
                p->selected = p->rows[i].id;
                rebind(p, scene, p->selected);
                p->dirty = 1;
                return ui_handled(1, 0);
            }
            return ui_unhandled();
        }
    }
    return ui_unhandled();
}

int ui_panels_build(UiPanels *p, ScWorld *scene, int w, int h, const UiFrame **out) {
    if (!p || !out) return 0;
    // Hot-reload темы по mtime (дешёвый stat, раз в 64 сборки).
    {
        static unsigned calls = 0;
        if ((++calls & 63) == 0 && ui_theme_poll(&p->theme, THEME_PATH)) p->dirty = 1;
    }
    // Новый размер окна = новая геометрия (иначе панели в старом размере).
    if (w != p->winW || h != p->winH) p->dirty = 1;
    if (!p->dirty) {
        *out = &p->frame;
        return 0;
    }
    p->frame.n = 0;
    p->winW = w;
    p->winH = h;
    // Менюбар + тулбар (во всю ширину, как UE).
    emit(p, 0, 0, (float)w, 26, p->theme.bar);
    emit(p, 0, 26, (float)w, 32, p->theme.bg);
    for (int i = 0; i < 4; i++)
        if (i == p->tool) emit(p, 8 + (float)(i * 64), 28, 60, 28, p->theme.tool);
    int hx = p->showHier ? p->hierW : 0;
    int ix = p->showInsp ? p->inspW : 0;
    // Иерархия: фон + строки + кнопка коллапса («<» прячет полностью, не в полоску).
    if (hx > 0) {
        emit(p, 0, 58, (float)hx, (float)(h - 58), p->theme.bg);
        emit(p, (float)(hx - 28), 58, 28, 24, p->theme.bar);
    }
    p->nrows = 0;
    if (scene && p->showHier) {
        Entity list[SC_MAX];
        int n = sc_list(scene, list, SC_MAX);
        float y = 64;
        for (int i = 0; i < n && p->nrows < PN_MAXROWS && y + 24 < h; i++) {
            int d = depth_of(scene, list[i]);
            float rx = 8 + (float)(d * 16);
            if (list[i] == p->selected)
                emit(p, 4, y, (float)(hx - 8), 24, p->theme.sel);
            if (p->nrows < PN_MAXROWS) {
                p->rows[p->nrows].x = rx;
                p->rows[p->nrows].y = y;
                p->rows[p->nrows].w = (float)(hx - (int)rx);
                p->rows[p->nrows].h = 24;
                p->rows[p->nrows].id = list[i];
                p->rows[p->nrows].depth = d;
                p->nrows++;
            }
            y += 24;
        }
    }
    // Статусбар (от иерархии до инспектора).
    emit(p, (float)hx, (float)(h - 26), (float)(w - hx - ix), 26, p->theme.bar);
    // Инспектор: правая панель + 9 строк + коллапс («>» прячет полностью).
    if (ix > 0) {
        emit(p, (float)(w - ix), 58, (float)ix, (float)(h - 84), p->theme.bg);
        emit(p, (float)(w - ix), 58, 28, 24, p->theme.bar);
    }
    for (int i = 0; i < PN_NF; i++) {
        p->frows[i].x = 0;
        p->frows[i].w = 0; // нет строк без панели (хит выключен)
        p->frows[i].field = i;
    }
    if (p->showInsp) {
        // Сетка: заголовок группы (16px) + 3 строки по 26px. Не налезают.
        for (int i = 0; i < PN_NF; i++) {
            float fx0 = (float)(w - ix + 4);
            float fy = 116 + (float)((i / 3) * 110 + (i % 3) * 26);
            if (fy + 24 > h - 26) break;
            emit(p, fx0, fy, (float)(ix - 8), 24,
                 i == p->scrubField ? p->theme.sel : p->theme.bar);
            p->frows[i].x = fx0;
            p->frows[i].y = fy;
            p->frows[i].w = (float)(ix - 8);
            p->frows[i].h = 24;
            p->frows[i].field = i;
        }
    }
    p->dirty = 0;
    *out = &p->frame;
    return 1;
}

void ui_panels_text(UiPanels *p, ScWorld *scene, UiText *text, int w, int h) {
    if (!p || !text) return;
    int hx = p->showHier ? p->hierW : 0;
    int ix = p->showInsp ? p->inspW : 0;
    // Оси в цветах гизмо (x красный, y зелёный, z синий) — цифры читаются.
    static const float AXC[3][4] = {{1.0f, 0.52f, 0.47f, 1.0f},
                                    {0.55f, 1.0f, 0.58f, 1.0f},
                                    {0.55f, 0.68f, 1.0f, 1.0f}};
    ui_text_quad(text, 12, 4, "File  Edit  View", p->theme.dim);
    if (p->flash[0]) ui_text_quad(text, 220, 4, p->flash, p->theme.dim);
    static const char *tools[4] = {"Q:Sel", "W:Move", "E:Rot", "R:Scale"};
    for (int i = 0; i < 4; i++)
        ui_text_quad(text, 14 + (float)(i * 64), 32, tools[i], i == p->tool ? p->theme.tool : p->theme.txt);
    // Кнопки коллапса («<» прячет, «>» возвращает).
    ui_text_quad(text, (float)(hx - 20), 62, p->showHier ? "<" : ">", p->theme.dim);
    ui_text_quad(text, (float)(w - ix + 8), 62, p->showInsp ? ">" : "<", p->theme.dim);
    if (p->showHier) {
        for (int i = 0; i < p->nrows; i++) {
            const char *nm = scene ? sc_name(scene, p->rows[i].id) : "?";
            char buf[80];
            snprintf(buf, sizeof buf, "%s", nm && nm[0] ? nm : "?");
            ui_text_quad(text, p->rows[i].x + 4, p->rows[i].y + 4, buf, p->theme.txt);
        }
    }
    // Статус: selected + tool (от иерархии до инспектора).
    char st[128];
    const char *sel = "?";
    if (scene && p->selected) {
        const char *nm = sc_name(scene, p->selected);
        if (nm && nm[0]) sel = nm;
    }
    snprintf(st, sizeof st, "sel: %s | tool: %d | Tab: panels", sel, p->tool);
    ui_text_quad(text, (float)(hx + 10), (float)(h - 22), st, p->theme.dim);
    // Инспектор: имя + группы Position/Rotation/Scale + значения в цветах осей.
    if (p->showInsp) {
        float ix0 = (float)(w - ix);
        if (scene && p->selected && sc_alive(scene, p->selected)) {
            ui_text_quad(text, ix0 + 34, 62, sel, p->theme.txt);
            static const char *groups[3] = {"Position", "Rotation", "Scale"};
            for (int i = 0; i < PN_NF; i++) {
                if (i % 3 == 0)
                    ui_text_quad(text, p->frows[i].x + 6, p->frows[i].y - 15, groups[i / 3],
                                 p->theme.dim);
                char vb[64];
                snprintf(vb, sizeof vb, "%c %.2f", "XYZ"[i % 3],
                         ui_attr_f_get(&p->binds[i]));
                ui_text_quad(text, p->frows[i].x + 6, p->frows[i].y + 5, vb, AXC[i % 3]);
            }
        } else {
            ui_text_quad(text, ix0 + 34, 62, "no selection", p->theme.dim);
        }
    }
}

unsigned ui_panels_selected(const UiPanels *p) {
    return p ? p->selected : 0;
}

UiUndo *ui_panels_undo(UiPanels *p) {
    return p ? &p->undo : 0;
}

// Трансформ трогали скрабом (тесту надо перекачать суп). Сбрасывается чтением.
int ui_panels_posed(UiPanels *p) {
    if (!p) return 0;
    int v = p->posed;
    p->posed = 0;
    return v;
}

int ui_panels_tool(const UiPanels *p) {
    return p ? p->tool : 0;
}
