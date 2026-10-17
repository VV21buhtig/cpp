// Раскладка 1280x720 (одна тема, без светло/тёмно-возни):
// менюбар y0-26 | тулбар y26-58 | иерархия x0-250 y58-720 | статус y694-720.
// Цвета: фон панелей тёмный, шапки светлее, select синий, tool активный оранжевый.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

// Привязка style-строки к теме (§12: 0 round 1 pad 2 bg.a 3 bar.a).
typedef struct {
    UiPanels *p;
    int field;
} UiThemeRef;

#define PN_NS 4 // style-строк в EDIT (скругление/отступ/альфы)

// Снимок раскладки (§11 хвост).
typedef struct {
    int hierW, inspW, showHier, showInsp, swapped;
} LaySnap;

struct UiPanels {
    unsigned selected;
    int tool;
    int dirty;
    int posed; // трансформ сущности меняли (тесту надо перекачать суп)
    int showHier; // 1 панель иерархии (0 = 0px, вьюпорту всё место)
    int showInsp; // 1 панель инспектора (0 = 0px)
    int hierW; // ширина иерархии (220)
    int inspW; // ширина инспектора (220)
    int swapped; // 1 панели менялись местами (§11: инспектор слева)
    int layDrag; // 0 нет, 1 тащим левую шапку, 2 правую (EDIT, drop=сторона)
    float layX0; // x DOWN шапки
    int dropLeft; // 1 дроп слева (подсветка цели в статусе)
    LaySnap layHist[16]; // история раскладки (Ctrl+Z в EDIT)
    int layN;
    int splitDrag; // 0 нет, 1 тащим край иерархии, 2 край инспектора (SSplitter)
    float splitX0; // x DOWN
    int splitW0; // ширина на DOWN
    UiFrame frame;
    char flash[64]; // последняя команда меню (File stub и т.п.)
    PnRow rows[PN_MAXROWS];
    int nrows;
    PnField frows[PN_NF];
    int scrubField; // -1 нет модального скраба (захват мыши как CaptureMouse)
    float scrubWas; // значение на DOWN (для undo-коммита на UP)
    float scrubX0;
    int scrubKind; // 0 ECS-бинды (undo), 1 theme-бинды (save §12)
    PnField srows[PN_NS]; // хитбоксы style-строк (EDIT)
    UiAttrF sbinds[PN_NS]; // бинды на тему
    UiThemeRef srefs[PN_NS];
    int editMode; // 1 ручной режим (§11): строки инертны, живут сплиттер/коллапс
    UiTheme theme; // одна тема (hot-reload по mtime)
    UiUndo undo; // стек команд (Фаза 5)
    UiEcsRef erefs[PN_NF]; // привязки строк инспектора к ECS (TAttribute)
    UiAttrF binds[PN_NF];
    int winW, winH; // размер окна с последней сборки (для хит-боксов справа)
};

#define THEME_PATH "engine/assets/theme.json"
#define LAYOUT_PATH "engine/assets/layout.json"

// Снимок раскладки (§11 хвост, дубль верхнего — struct раньше).
static void dock(const UiPanels *p, int W, int *lw, int *rw, int *hox, int *iox) {
    int h = p->showHier ? p->hierW : 0;
    int i = p->showInsp ? p->inspW : 0;
    int leftHier = !p->swapped;
    if (lw) *lw = leftHier ? h : i;
    if (rw) *rw = leftHier ? i : h;
    if (hox) *hox = leftHier ? 0 : W - h;
    if (iox) *iox = leftHier ? W - i : 0;
}

// Снимок раскладки в историю (без дублей подряд).
static void lay_push(UiPanels *p) {
    if (!p) return;
    if (p->layN > 0) {
        const LaySnap *t = &p->layHist[p->layN - 1];
        if (t->hierW == p->hierW && t->inspW == p->inspW && t->showHier == p->showHier &&
            t->showInsp == p->showInsp && t->swapped == p->swapped)
            return;
    }
    if (p->layN >= 16) {
        memmove(&p->layHist[0], &p->layHist[1], sizeof p->layHist[0] * 15);
        p->layN = 15;
    }
    LaySnap *s = &p->layHist[p->layN++];
    s->hierW = p->hierW;
    s->inspW = p->inspW;
    s->showHier = p->showHier;
    s->showInsp = p->showInsp;
    s->swapped = p->swapped;
}

static long g_layMtime = 0;

static int lay_num(const char *s, const char *key, int *out) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    f = strchr(f, '[');
    if (!f) return 0;
    return sscanf(f, "[%d", out) == 1;
}

static void lay_load(UiPanels *p, const char *path) {
    if (!p || !path) return;
    FILE *ff = fopen(path, "rb");
    if (!ff) return;
    fseek(ff, 0, SEEK_END);
    long sz = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    if (sz <= 0 || sz > 4096) {
        fclose(ff);
        return;
    }
    char s[4096];
    if (fread(s, 1, (size_t)sz, ff) != (size_t)sz) {
        fclose(ff);
        return;
    }
    s[sz] = 0;
    fclose(ff);
    int v;
    if (lay_num(s, "hierW", &v) && v >= 120 && v <= 400) p->hierW = v;
    if (lay_num(s, "inspW", &v) && v >= 120 && v <= 400) p->inspW = v;
    if (lay_num(s, "showHier", &v)) p->showHier = v ? 1 : 0;
    if (lay_num(s, "showInsp", &v)) p->showInsp = v ? 1 : 0;
    if (lay_num(s, "swapped", &v)) p->swapped = v ? 1 : 0;
    struct stat st;
    if (stat(path, &st) == 0) g_layMtime = (long)st.st_mtime;
    p->dirty = 1;
}

// ОДИН файл layout.json — ДВА писателя: §11 доки (здесь) и §14 виджеты
// (widget_json_save). Пишем только СВОИ ключи, а чужую секцию "widgets"
// переносим как есть (read-modify-write, без клоббера). Скан без парсера:
// массив верхнего уровня, строки с экранированием — этого достаточно.
#define LAY_FILE_CAP 65536
static int lay_widgets_span(const char *s, const char **a, const char **b) {
    const char *p = strstr(s, "\"widgets\"");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    int depth = 0, in = 0;
    for (const char *q = p; *q; q++) {
        char c = *q;
        if (in) {
            if (c == '\\') { q++; continue; }
            if (c == '"') in = 0;
            continue;
        }
        if (c == '"') { in = 1; continue; }
        if (c == '[') depth++;
        else if (c == ']' && --depth == 0) { *a = p; *b = q; return 1; }
    }
    return 0;
}

static int lay_save(const UiPanels *p, const char *path) {
    if (!p || !path) return 0;
    char dock[512];
    int dn = snprintf(dock, sizeof dock,
            "{\n"
            "  \"_note\": \"editor layout (§11): widths/visibility/swap. Hot-reload by mtime.\",\n"
            "  \"hierW\": [%d],\n"
            "  \"inspW\": [%d],\n"
            "  \"showHier\": [%d],\n"
            "  \"showInsp\": [%d],\n"
            "  \"swapped\": [%d]",
            p->hierW, p->inspW, p->showHier, p->showInsp, p->swapped);
    if (dn <= 0 || (size_t)dn >= sizeof dock) return 0;
    char file[LAY_FILE_CAP + 1];
    file[0] = 0;
    FILE *rf = fopen(path, "rb");
    if (rf) {
        size_t got = fread(file, 1, LAY_FILE_CAP, rf);
        file[got] = 0;
        fclose(rf);
    }
    const char *a = 0, *b = 0;
    char out[LAY_FILE_CAP + 1024];
    int m;
    if (file[0] && lay_widgets_span(file, &a, &b))
        m = snprintf(out, sizeof out, "%s,\n  \"widgets\": %.*s\n}\n", dock,
                     (int)(b - a + 1), a); // чужая секция — как есть
    else
        m = snprintf(out, sizeof out, "%s\n}\n", dock);
    if (m <= 0 || (size_t)m >= sizeof out) return 0;
    FILE *ff = fopen(path, "wb");
    if (!ff) return 0;
    int ok = fwrite(out, 1, (size_t)m, ff) == (size_t)m;
    fclose(ff);
    if (!ok) return 0;
    struct stat st;
    if (stat(path, &st) == 0) g_layMtime = (long)st.st_mtime;
    return 1;
}

int ui_panels_save_layout(UiPanels *p, const char *path) { return lay_save(p, path); }

static void lay_commit(UiPanels *p) {
    // Коммит раскладки: история + файл (§11 хвост).
    lay_push(p);
    lay_save(p, LAYOUT_PATH);
}

static void lay_poll(UiPanels *p, const char *path) {
    struct stat st;
    if (!p || !path || stat(path, &st) != 0) return;
    if ((long)st.st_mtime == g_layMtime) return;
    lay_load(p, path);
}

static int lay_undo(UiPanels *p) {
    if (!p || p->layN <= 1) return 0; // нижний — стартовый, не откатываем
    p->layN--;
    const LaySnap *s = &p->layHist[p->layN - 1];
    p->hierW = s->hierW;
    p->inspW = s->inspW;
    p->showHier = s->showHier;
    p->showInsp = s->showInsp;
    p->swapped = s->swapped;
    p->dirty = 1;
    return 1;
}

static float theme_get(void *user);
static void theme_set(void *user, float v);

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
    lay_load(p, LAYOUT_PATH); // нет файла — дефолт, не ошибка
    lay_push(p); // стартовый снимок (дно истории undo)
    for (int i = 0; i < PN_NS; i++) { // style-бинды на тему (§12, живут с панелью)
        p->srefs[i].p = p;
        p->srefs[i].field = i;
        p->sbinds[i].bound = 1;
        p->sbinds[i].value = 0.0f;
        p->sbinds[i].get = theme_get;
        p->sbinds[i].set = theme_set;
        p->sbinds[i].user = &p->srefs[i];
    }
    ui_undo_reset(&p->undo);
    return p;
}

// Прямоугольник вьюпорта (док-модель UE: остаток после панелей).
// Пишет x,y,w,h (y вниз). Вызывающий пересоздаёт ТОЛЬКО цель (не ядро/окно).
void ui_panels_viewport(const UiPanels *p, int W, int H, int *x, int *y, int *w, int *h) {
    int lw = 0, rw = 0;
    if (p) dock(p, W, &lw, &rw, 0, 0);
    if (x) *x = lw;
    if (y) *y = 58;
    if (w) *w = W - lw - rw;
    if (h) *h = H - 58 - 26;
}

void ui_panels_destroy(UiPanels *p) {
    free(p);
}

static void emit(UiPanels *p, float x, float y, float w, float h, const float *c, float r) {
    if (p->frame.n >= UI_MAX_PRIMS) return;
    UiPrim *q = &p->frame.prims[p->frame.n++];
    q->kind = 0;
    q->a = x;
    q->b = y;
    q->c = w;
    q->d = h;
    q->e = r; // радиус §12 (0 = остро)
    q->f = 0;
    memcpy(q->rgba, c, sizeof q->rgba);
}
#define RD (p->theme.round) // радиус хрома (панели/бары), строки острые

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

// Живые значения стиля из темы (§12).
static float theme_get(void *user) {
    UiThemeRef *r = (UiThemeRef *)user;
    if (!r || !r->p) return 0.0f;
    const UiTheme *t = &r->p->theme;
    switch (r->field) {
    case 0: return t->round;
    case 1: return t->pad;
    case 2: return t->bg[3];
    default: return t->bar[3];
    }
}

static void theme_set(void *user, float v) {
    UiThemeRef *r = (UiThemeRef *)user;
    if (!r || !r->p) return;
    UiTheme *t = &r->p->theme;
    switch (r->field) {
    case 0: // round 0..24
        t->round = v < 0 ? 0 : (v > 24 ? 24 : v);
        break;
    case 1: // pad 0..32
        t->pad = v < 0 ? 0 : (v > 32 ? 32 : v);
        break;
    case 2: // bg.a 0..1
        t->bg[3] = v < 0 ? 0 : (v > 1 ? 1 : v);
        break;
    default: // bar.a 0..1
        t->bar[3] = v < 0 ? 0 : (v > 1 ? 1 : v);
        break;
    }
    r->p->dirty = 1;
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
    // Kind 0: ECS (undo), 1: тема (save §12).
    if (p->scrubField >= 0) {
        UiAttrF *b = p->scrubKind ? &p->sbinds[p->scrubField] : &p->binds[p->scrubField];
        if (e->kind == UI_EV_MOVE) {
            float v = p->scrubWas + (e->x - p->scrubX0) * 0.05f;
            ui_attr_f_set(b, v);
            if (!p->scrubKind) p->posed = 1; // ECS: супу перекачка; теме не надо
            p->dirty = 1;
            return ui_handled(1, 1); // держим захват до UP
        }
        if (e->kind == UI_EV_UP) {
            if (p->scrubKind) {
                ui_theme_save(&p->theme, THEME_PATH); // файл и есть история
            } else {
                // Коммит mouse-up: {было -> стало} в undo (как Unity flush).
                float now = ui_attr_f_get(b);
                ui_undo_push(&p->undo, p->selected, p->scrubField, p->scrubWas, now);
            }
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
            lay_commit(p); // раскладка поменялась — в историю и файл
            return ui_handled(1, 0);
        }
        // Ctrl+Z откат, Ctrl+Shift+Z / Ctrl+Y повтор (как везде).
        // В EDIT: откат раскладки (§11 хвост), не ECS.
        if ((e->mods & 2) && (e->key == 'Z' || e->key == 'Y')) {
            if (p->editMode) {
                if (e->key == 'Z' && !(e->mods & 1) && lay_undo(p)) {
                    lay_save(p, LAYOUT_PATH);
                    return ui_handled(1, 0);
                }
                return ui_unhandled();
            }
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
        // L: ручной режим (§11, Figma-руками, код 0). Без модов.
        if (e->key == 'L' && !(e->mods & 6)) {
            p->editMode = !p->editMode;
            p->splitDrag = 0; // выход из сплиттера при переключении
            p->layDrag = 0; // и из drag шапки
            p->scrubField = -1; // и из скраба
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    // Модальный сплиттер раньше контента (тащим границу — едет ширина).
    // 1 = левый край, 2 = правый (панель — кто живёт на стороне, swap-aware).
    if (p->splitDrag) {
        int leftHier = !p->swapped;
        int edgeIsHier = (p->splitDrag == 1) ? leftHier : !leftHier;
        if (e->kind == UI_EV_MOVE) {
            int w = p->splitW0 + (int)(p->splitDrag == 1 ? e->x - p->splitX0
                                                         : p->splitX0 - e->x);
            if (w < 120) w = 120;
            if (w > 400) w = 400;
            if (edgeIsHier) p->hierW = w;
            else p->inspW = w;
            p->dirty = 1;
            return ui_handled(1, 1);
        }
        if (e->kind == UI_EV_UP) {
            p->splitDrag = 0;
            p->dirty = 1;
            lay_commit(p); // ширина — часть раскладки
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    // Модальный драг шапки (EDIT): тащим панель на другую сторону (drop).
    if (p->layDrag) {
        if (e->kind == UI_EV_MOVE) {
            p->dropLeft = e->x < p->winW / 2 ? 1 : 0;
            p->dirty = 1; // статус показывает цель
            return ui_handled(1, 1);
        }
        if (e->kind == UI_EV_UP) {
            int curLeft = (p->layDrag == 1) ? 1 : 0; // откуда тащили
            if (curLeft != p->dropLeft) {
                p->swapped = !p->swapped; // уронили на другую сторону
                lay_commit(p);
            }
            p->layDrag = 0;
            p->dirty = 1;
            return ui_handled(1, 0);
        }
        return ui_unhandled();
    }
    if (e->kind != UI_EV_DOWN || e->button != 0) return ui_unhandled();
    float x = e->x, y = e->y;
    int lw = 0, rw = 0, hox = 0, iox = 0;
    dock(p, p->winW, &lw, &rw, &hox, &iox);
    int leftHier = !p->swapped;
    // Сплиттер раньше строк (SSplitter: тащим край — едет ширина, 120..400).
    // В EDIT захват шире (12px): рукой попасть проще (закон Фиттса).
    if (y >= 58 && y < p->winH) {
        int grab = p->editMode ? 12 : 6;
        if (lw > 0 && x >= lw - grab && x < lw) {
            p->splitDrag = 1; // левый край
            p->splitX0 = x;
            p->splitW0 = leftHier ? p->hierW : p->inspW;
            return ui_handled(0, 1);
        }
        if (rw > 0 && x >= p->winW - rw && x < p->winW - rw + grab) {
            p->splitDrag = 2; // правый край
            p->splitX0 = x;
            p->splitW0 = leftHier ? p->inspW : p->hierW;
            return ui_handled(0, 1);
        }
    }
    // Драг шапки (только EDIT): уронить панель на другую сторону.
    if (p->editMode && y >= 58 && y < 82) {
        if (lw > 0 && x >= 0 && x < lw - 28) {
            p->layDrag = 1;
            p->layX0 = x;
            p->dropLeft = 1;
            return ui_handled(0, 1);
        }
        if (rw > 0 && x >= p->winW - rw + 28 && x < p->winW) {
            p->layDrag = 2;
            p->layX0 = x;
            p->dropLeft = 0;
            return ui_handled(0, 1);
        }
    }
    // Кнопки коллапса раньше контента (хром первее строк).
    // Закрытая панель = 0px (возврат через Tab/View, полосок нет).
    if (y >= 58 && y < 82) {
        if (lw > 0 && x >= lw - 28 && x < lw) {
            if (leftHier) p->showHier = 0;
            else p->showInsp = 0;
            p->dirty = 1;
            lay_commit(p);
            return ui_handled(1, 0);
        }
        if (rw > 0 && x >= p->winW - rw && x < p->winW - rw + 28) {
            if (leftHier) p->showInsp = 0;
            else p->showHier = 0;
            p->dirty = 1;
            lay_commit(p);
            return ui_handled(1, 0);
        }
    }
    // Инспектор: DOWN на строке поля = начать скраб через бинд (коммит на UP).
    // В ручном режиме строки инертны (§11): живут только сплиттер/коллапс.
    if (!p->editMode && scene && p->selected && sc_alive(scene, p->selected)) {
        for (int i = 0; i < PN_NF; i++) {
            if (x >= p->frows[i].x && x < p->frows[i].x + p->frows[i].w &&
                y >= p->frows[i].y && y < p->frows[i].y + p->frows[i].h) {
                rebind(p, scene, p->selected);
                p->scrubField = p->frows[i].field;
                p->scrubKind = 0; // ECS: коммит в undo
                p->scrubWas = ui_attr_f_get(&p->binds[p->scrubField]);
                p->scrubX0 = x;
                return ui_handled(0, 1); // тихо + захват (грязь будет на MOVE)
            }
        }
    }
    // EDIT: DOWN на style-строке = скраб темы (коммит на UP пишет файл).
    if (p->editMode) {
        for (int i = 0; i < PN_NS; i++) {
            if (x >= p->srows[i].x && x < p->srows[i].x + p->srows[i].w &&
                y >= p->srows[i].y && y < p->srows[i].y + p->srows[i].h) {
                p->scrubField = i;
                p->scrubKind = 1; // тема: коммит в файл
                p->scrubWas = ui_attr_f_get(&p->sbinds[i]);
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
            lay_commit(p);
            return ui_handled(1, 0);
        }
        const char *m = x < 70 ? "File" : "Edit";
        snprintf(p->flash, sizeof p->flash, "%s (stub)", m);
        p->dirty = 1;
        return ui_handled(1, 0);
    }
    // Иерархия: строки (смена selection перепривязывает инспектор).
    // В ручном режиме инертны (§11): клик по строке ничего не меняет.
    if (!p->editMode)
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
    // Hot-reload темы и раскладки по mtime (дешёвый stat, раз в 64 сборки).
    {
        static unsigned calls = 0;
        if ((++calls & 63) == 0) {
            if (ui_theme_poll(&p->theme, THEME_PATH)) p->dirty = 1;
            long before = g_layMtime;
            lay_poll(p, LAYOUT_PATH);
            if (g_layMtime != before) lay_push(p); // внешняя правка — в историю
        }
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
    emit(p, 0, 0, (float)w, 26, p->theme.bar, RD);
    emit(p, 0, 26, (float)w, 32, p->theme.bg, RD);
    for (int i = 0; i < 4; i++)
        if (i == p->tool) emit(p, 8 + (float)(i * 64), 28, 60, 28, p->theme.tool, 0.0f);
    int hx = 0, ix = 0, hox = 0, iox = 0; // hx=lw, ix=rw; hox/iox начала панелей
    dock(p, w, &hx, &ix, &hox, &iox);
    // Иерархия: фон + строки + кнопка коллапса («<» прячет полностью, не в полоску).
    if (p->showHier) {
        emit(p, (float)hox, 58, (float)p->hierW, (float)(h - 58), p->theme.bg, RD);
        emit(p, (float)(hox + p->hierW - 28), 58, 28, 24, p->theme.bar, 0.0f);
    }
    p->nrows = 0;
    float pad = p->theme.pad; // отступ строк (§12, 0 = legacy)
    if (scene && p->showHier) {
        Entity list[SC_MAX];
        int n = sc_list(scene, list, SC_MAX);
        float y = 64;
        for (int i = 0; i < n && p->nrows < PN_MAXROWS && y + 24 < h; i++) {
            int d = depth_of(scene, list[i]);
            float rx = (float)hox + 8 + (float)(d * 16) + pad;
            if (list[i] == p->selected)
                emit(p, (float)hox + 4, y, (float)(p->hierW - 8), 24, p->theme.sel, 0.0f);
            if (p->nrows < PN_MAXROWS) {
                p->rows[p->nrows].x = rx;
                p->rows[p->nrows].y = y;
                p->rows[p->nrows].w = (float)(hox + p->hierW) - rx;
                p->rows[p->nrows].h = 24;
                p->rows[p->nrows].id = list[i];
                p->rows[p->nrows].depth = d;
                p->nrows++;
            }
            y += 24;
        }
    }
    // Статусбар (от иерархии до инспектора).
    emit(p, (float)hx, (float)(h - 26), (float)(w - hx - ix), 26, p->theme.bar, RD);
    // Инспектор: правая панель + 9 строк + коллапс («>» прячет полностью).
    if (p->showInsp) {
        emit(p, (float)iox, 58, (float)p->inspW, (float)(h - 84), p->theme.bg, RD);
        emit(p, (float)iox, 58, 28, 24, p->theme.bar, 0.0f);
    }
    for (int i = 0; i < PN_NF; i++) {
        p->frows[i].x = 0;
        p->frows[i].w = 0; // нет строк без панели (хит выключен)
        p->frows[i].field = i;
    }
    for (int i = 0; i < PN_NS; i++) {
        p->srows[i].x = 0;
        p->srows[i].w = 0; // хит style-строк только в EDIT
        p->srows[i].field = i;
    }
    if (p->showInsp && p->editMode) {
        // EDIT: 4 style-строки вместо transform (§12).
        for (int i = 0; i < PN_NS; i++) {
            float fx0 = (float)(iox + 4);
            float fy = 116 + (float)(i * 26);
            if (fy + 24 > h - 26) break;
            emit(p, fx0, fy, (float)(ix - 8), 24,
                 i == p->scrubField && p->scrubKind ? p->theme.sel : p->theme.bar, 0.0f);
            p->srows[i].x = fx0;
            p->srows[i].y = fy;
            p->srows[i].w = (float)(ix - 8);
            p->srows[i].h = 24;
            p->srows[i].field = i;
        }
    } else if (p->showInsp) {
        // Сетка: заголовок группы (16px) + 3 строки по 26px. Не налезают.
        for (int i = 0; i < PN_NF; i++) {
            float fx0 = (float)(iox + 4) + pad;
            float fw = (float)(p->inspW - 8) - 2 * pad;
            if (fw < 8) fw = 8;
            float fy = 116 + (float)((i / 3) * 110 + (i % 3) * 26);
            if (fy + 24 > h - 26) break;
            emit(p, fx0, fy, fw, 24,
                 i == p->scrubField && !p->scrubKind ? p->theme.sel : p->theme.bar, 0.0f);
            p->frows[i].x = fx0;
            p->frows[i].y = fy;
            p->frows[i].w = fw;
            p->frows[i].h = 24;
            p->frows[i].field = i;
        }
    }
    // Ручки EDIT: видимые полосы на краях (рука видит за что тянуть).
    if (p->editMode) {
        if (hx > 0) emit(p, (float)(hx - 3), 58, 6, (float)(h - 84), p->theme.sel, 0.0f);
        if (ix > 0) emit(p, (float)(w - ix - 3), 58, 6, (float)(h - 84), p->theme.sel, 0.0f);
    }
    p->dirty = 0;
    *out = &p->frame;
    return 1;
}

void ui_panels_text(UiPanels *p, ScWorld *scene, UiText *text, int w, int h) {
    if (!p || !text) return;
    int hx = 0, ix = 0, hox = 0, iox = 0;
    dock(p, w, &hx, &ix, &hox, &iox);
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
    ui_text_quad(text, (float)(hox + (p->showHier ? p->hierW : 0) - 20), 62,
                 p->showHier ? "<" : ">", p->theme.dim);
    ui_text_quad(text, (float)(iox + 8), 62, p->showInsp ? ">" : "<", p->theme.dim);
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
    snprintf(st, sizeof st, "sel: %s | tool: %d | Tab: panels%s%s", sel, p->tool,
             p->editMode ? " | EDIT: drag blue edges | L exits" : "",
             p->layDrag ? (p->dropLeft ? " | drop: LEFT" : " | drop: RIGHT") : "");
    ui_text_quad(text, (float)(hx + 10), (float)(h - 22), st, p->theme.dim);
    // Инспектор: имя + группы Position/Rotation/Scale + значения в цветах осей.
    // В EDIT — стиль интерфейса (§12): round/pad/alpha руками.
    if (p->showInsp) {
    float ix0 = (float)iox;
        if (p->editMode) {
            ui_text_quad(text, ix0 + 34, 62, "UI style", p->theme.txt);
            static const char *snames[PN_NS] = {"round", "pad", "bg.a", "bar.a"};
            for (int i = 0; i < PN_NS; i++) {
                char vb[64];
                snprintf(vb, sizeof vb, "%s %.2f", snames[i], ui_attr_f_get(&p->sbinds[i]));
                ui_text_quad(text, p->srows[i].x + 6, p->srows[i].y + 5, vb, p->theme.txt);
            }
        } else if (scene && p->selected && sc_alive(scene, p->selected)) {
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

int ui_panels_edit(const UiPanels *p) {
    return p ? p->editMode : 0;
}

// Ховер хрома для курсора (тест/вью: над краем = ресайз-курсор).
int ui_panels_hover(const UiPanels *p, float x, float y) {
    if (!p) return 0;
    int lw = 0, rw = 0;
    dock(p, p->winW, &lw, &rw, 0, 0);
    int grab = p->editMode ? 12 : 6;
    if (y >= 58 && y < p->winH) {
        if (lw > 0 && x >= lw - grab && x < lw) return 1;
        if (rw > 0 && x >= p->winW - rw && x < p->winW - rw + grab) return 1;
    }
    if (y >= 58 && y < 82) {
        if (lw > 0 && x >= lw - 28 && x < lw) return 1;
        if (rw > 0 && x >= p->winW - rw && x < p->winW - rw + 28) return 1;
    }
    return 0;
}
