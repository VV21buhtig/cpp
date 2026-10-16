// ui_edit: отдельный правщик фронта редактора (§13). Своё окно, без 3D:
// панели (UiPanels, всегда EDIT) + плейсхолдер вьюпорта + хинт.
// НЕ режим внутри игрового редактора (там игре — игра, правке — правка).
// Файлы-обменники с игровым редактором: layout.json + theme.json
// (коммиты пишут сами панели: lay_commit / ui_theme_save).
// Запуск из корня репо (шейдеры относительные, как ui_test): ./engine/build/ui_edit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhi_gl.h" // ПЕРВЫМ: GLFW_INCLUDE_NONE + glad (иначе gl.h-конфликт)
#include "render_api.h"
#include "ui/ui_panels.h" // меню/тулбар/иерархия/статус + EDIT-правка
#include "ui/ui_widget.h" // дерево виджетов конструктора (§14)
#include "ui/ui_text.h" // подписи + хинт инструмента
#include "rtarget.h"
#include "comp_gl.h"
#include "comp.h"
#include "rhi_gl.h"

#define W 1280
#define H 720 // дефолт до первого замера

static int g_WW = W, g_WH = H; // живой размер (фреймбуферные пиксели)
static float g_mouseKx = 1.0f, g_mouseKy = 1.0f;
static double g_mx = 0, g_my = 0;
static UiPanels *g_panels = 0;
static RenderCore *g_vec = 0;
static ScWorld g_scene;
static GLFWcursor *g_curArrow = 0, *g_curResize = 0;
static int g_quit = 0;
// Rect плейсхолдера вьюпорта (из последнего push — для подписи).
static int g_vpx = 0, g_vpy = 0, g_vpw = 0, g_vph = 0;

// --- §14 F2a: дерево виджетов + палитра (tool chrome, не модель) ---
// Холст = всё окно (WYSIWYG: доки правятся тем же UiPanels). Палитра — поверх
// вьюпорта. Пока только в памяти: в layout.json не пишем (персист — след. шаг,
// иначе конфликт писателей layout.json с доками).
static UiWidgetTree g_wt;
static int g_wtSel = 0;   // выбранный виджет (id, 0 = нет)
static int g_palHot = -1; // под палитрой (для подсветки)
static int g_wtN[3] = {0, 0, 0}; // счётчики имён по типу

#define PAL_BW 104
#define PAL_BH 26
#define PAL_GAP 4
#define PAL_N 3
static const int PAL_TYPE[PAL_N] = {UI_W_PANEL, UI_W_BUTTON, UI_W_LABEL};
static const char *PAL_LABEL[PAL_N] = {"Panel", "Button", "Label"};

static void pal_rect(int i, float *x, float *y, float *w, float *h) {
    *x = (float)(g_vpx + 10);
    *y = (float)(g_vpy + 10 + i * (PAL_BH + PAL_GAP));
    *w = (float)PAL_BW;
    *h = (float)PAL_BH;
}

static int pal_hit(float px, float py) {
    for (int i = 0; i < PAL_N; i++) {
        float x, y, w, h;
        pal_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}

// Прямоугольник спавна (якорь L|T, в пикселях окна) — у вьюпорта, каскадом.
static UiWidgetRect wt_spawn_rect(int type) {
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    r.x = (float)(g_vpx + 130);
    r.y = (float)(g_vpy + 30 + g_wt.count * 34);
    if (type == UI_W_PANEL) {
        r.w = 240;
        r.h = 130;
    } else if (type == UI_W_BUTTON) {
        r.w = 120;
        r.h = 32;
    } else {
        r.w = 150;
        r.h = 22;
    }
    return r;
}

static UiWidgetStyle wt_spawn_style(int type) {
    UiWidgetStyle s;
    memset(&s, 0, sizeof s);
    s.alpha = 1.0f;
    s.fontSize = 14.0f;
    if (type == UI_W_PANEL) {
        s.bg[0] = 0.13f;
        s.bg[1] = 0.15f;
        s.bg[2] = 0.19f;
        s.bg[3] = 1.0f;
        s.rounding = 6.0f;
    } else if (type == UI_W_BUTTON) {
        s.bg[0] = 0.22f;
        s.bg[1] = 0.34f;
        s.bg[2] = 0.62f;
        s.bg[3] = 1.0f;
        s.rounding = 4.0f;
    } else { // label: подпись без плашки
        s.bg[3] = 0.0f;
    }
    return s;
}

static int wt_spawn(int type) {
    int k = type == UI_W_PANEL ? 0 : type == UI_W_BUTTON ? 1 : 2;
    g_wtN[k]++;
    char nm[UI_WIDGET_NAME];
    snprintf(nm, sizeof nm, "%s%d", k == 0 ? "Panel" : k == 1 ? "Button" : "Label",
             g_wtN[k]);
    UiWidgetRect r = wt_spawn_rect(type);
    UiWidgetStyle s = wt_spawn_style(type);
    int id = widget_add(&g_wt, type, nm, 0, &r, &s);
    if (id) g_wtSel = id;
    return id;
}

// Верхний (по id — дети выше родителей) под точкой.
static int wt_hit(float px, float py) {
    int best = 0;
    for (int i = 0; i < UI_MAX_WIDGETS; i++) {
        if (!g_wt.alive[i]) continue;
        float x, y, w, h;
        widget_rect_px(&g_wt.items[i], g_WW, g_WH, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) best = i + 1;
    }
    return best;
}

static void wt_emit(RcVecShape *sh, int *n, float x, float y, float w, float h,
                    const float rgba[4], float round) {
    if (*n >= UI_MAX_PRIMS) return;
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

static void wt_draw_node(RcVecShape *sh, int *n, int id) {
    const UiWidget *w = widget_cget(&g_wt, id);
    if (!w) return;
    float x, y, ww, hh;
    widget_rect_px(w, g_WW, g_WH, &x, &y, &ww, &hh);
    float col[4] = {w->style.bg[0], w->style.bg[1], w->style.bg[2],
                    w->style.bg[3] * w->style.alpha};
    if (col[3] > 0.001f) wt_emit(sh, n, x, y, ww, hh, col, w->style.rounding);
    for (int i = 0; i < w->nchildren; i++) wt_draw_node(sh, n, w->children[i]);
}

// Виджеты (корни->дети), затем рамка выбора. Палитра — отдельно (поверх).
static void wt_draw_all(RcVecShape *sh, int *n) {
    for (int i = 0; i < UI_MAX_WIDGETS; i++)
        if (g_wt.alive[i] && g_wt.items[i].parent == 0) wt_draw_node(sh, n, i + 1);
    const UiWidget *s = widget_cget(&g_wt, g_wtSel);
    if (s) {
        float x, y, ww, hh;
        widget_rect_px(s, g_WW, g_WH, &x, &y, &ww, &hh);
        static const float sel[4] = {0.25f, 0.55f, 1.0f, 1.0f};
        wt_emit(sh, n, x - 1, y - 1, ww + 2, 2, sel, 0);
        wt_emit(sh, n, x - 1, y + hh - 1, ww + 2, 2, sel, 0);
        wt_emit(sh, n, x - 1, y, 2, hh, sel, 0);
        wt_emit(sh, n, x + ww - 1, y, 2, hh, sel, 0);
        // 8 ручек ресайза (визуал «за что тянуть» — юзер просил).
        float hxs[3] = {x, x + ww * 0.5f, x + ww};
        float hys[3] = {y, y + hh * 0.5f, y + hh};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) {
                if (i == 1 && j == 1) continue; // центр — не ручка
                wt_emit(sh, n, hxs[i] - 3, hys[j] - 3, 6, 6, sel, 0);
            }
    }
}

static void pal_draw(RcVecShape *sh, int *n) {
    for (int i = 0; i < PAL_N; i++) {
        float x, y, w, h;
        pal_rect(i, &x, &y, &w, &h);
        float c[4] = {0.20f, 0.22f, 0.27f, 1.0f};
        if (i == g_palHot) {
            c[0] = 0.30f;
            c[1] = 0.45f;
            c[2] = 0.75f;
        }
        wt_emit(sh, n, x, y, w, h, c, 4.0f);
    }
}

// --- F2b: драг/ресайз (frozen-frame как гизмо: якорь на DOWN) ---
// Хэндл = битмаска краёв: L=1 R=2 T=4 B=8; 0 = перемещение.
static int g_dragId = 0;
static int g_dragHandle = 0;
static float g_dragMX = 0, g_dragMY = 0;             // мышь на DOWN (px окна)
static float g_dragX = 0, g_dragY = 0, g_dragW = 0, g_dragH = 0; // rect на DOWN

#define WT_GRAB 7.0f
#define WT_MIN 8.0f

// Хэндл ресайза под точкой (у выбранного). 0 = не хэндл (тело/мимо).
static int wt_handle_hit(float px, float py) {
    const UiWidget *s = widget_cget(&g_wt, g_wtSel);
    if (!s) return 0;
    float x, y, w, h;
    widget_rect_px(s, g_WW, g_WH, &x, &y, &w, &h);
    int loX = px >= x - 2 && px <= x + w + 2;
    int loY = py >= y - 2 && py <= y + h + 2;
    if (!loX || !loY) return 0;
    int hb = 0;
    if (px <= x + WT_GRAB) hb |= 1;
    if (px >= x + w - WT_GRAB) hb |= 2;
    if (py <= y + WT_GRAB) hb |= 4;
    if (py >= y + h - WT_GRAB) hb |= 8;
    return hb;
}

static void wt_drag_begin(int id, int handle, float mx, float my) {
    const UiWidget *w = widget_cget(&g_wt, id);
    if (!w) return;
    g_dragId = id;
    g_dragHandle = handle;
    g_dragMX = mx;
    g_dragMY = my;
    widget_rect_px(w, g_WW, g_WH, &g_dragX, &g_dragY, &g_dragW, &g_dragH);
}

// Обратно из пикселей в якоря (инверсия widget_rect_px).
static void pt_to_anchor(UiWidget *w, float px, float py, float pw, float ph, int Wpx,
                         int Hpx) {
    if (!w) return;
    w->rect.w = pw;
    w->rect.h = ph;
    if (w->rect.anchors & UI_A_LEFT) w->rect.x = px;
    else if (w->rect.anchors & UI_A_RIGHT) w->rect.x = (float)Wpx - px - pw;
    else w->rect.x = px - ((float)Wpx - pw) * 0.5f;
    if (w->rect.anchors & UI_A_TOP) w->rect.y = py;
    else if (w->rect.anchors & UI_A_BOTTOM) w->rect.y = (float)Hpx - py - ph;
    else w->rect.y = py - ((float)Hpx - ph) * 0.5f;
}

static void wt_drag_apply(float mx, float my) {
    UiWidget *w = widget_get(&g_wt, g_dragId);
    if (!w) {
        g_dragId = 0;
        return;
    }
    float dx = mx - g_dragMX, dy = my - g_dragMY;
    float nx = g_dragX, ny = g_dragY, nw = g_dragW, nh = g_dragH;
    if (g_dragHandle == 0) {
        nx += dx;
        ny += dy;
    } else {
        if (g_dragHandle & 1) {
            nx += dx;
            nw -= dx;
        }
        if (g_dragHandle & 2) nw += dx;
        if (g_dragHandle & 4) {
            ny += dy;
            nh -= dy;
        }
        if (g_dragHandle & 8) nh += dy;
        if (nw < WT_MIN) { // не даём вывернуться: фиксируем левый/верхний край
            if (g_dragHandle & 1) nx = g_dragX + g_dragW - WT_MIN;
            nw = WT_MIN;
        }
        if (nh < WT_MIN) {
            if (g_dragHandle & 4) ny = g_dragY + g_dragH - WT_MIN;
            nh = WT_MIN;
        }
    }
    pt_to_anchor(w, nx, ny, nw, nh, g_WW, g_WH);
}

// Событие в холст/палитру. 1 = наше (доки не трогаем). Спавн/выбор/драг — тут.
static int wt_event(const UiEvent *e) {
    if (e->kind == UI_EV_MOVE) {
        if (g_dragId) {
            wt_drag_apply(e->x, e->y);
            return 1;
        }
        int h = pal_hit(e->x, e->y);
        if (h != g_palHot) {
            g_palHot = h;
            return 1;
        }
        return 0;
    }
    if (e->kind == UI_EV_UP) {
        if (g_dragId) {
            g_dragId = 0;
            g_dragHandle = 0;
            return 1;
        }
        return 0;
    }
    if (e->kind != UI_EV_DOWN) return 0;
    int pi = pal_hit(e->x, e->y);
    if (pi >= 0) { // палитра поверх всего
        wt_spawn(PAL_TYPE[pi]);
        return 1;
    }
    int hb = wt_handle_hit(e->x, e->y);
    if (hb > 0 && g_wtSel) { // ручки выбранного — ресайз
        wt_drag_begin(g_wtSel, hb, e->x, e->y);
        return 1;
    }
    int id = wt_hit(e->x, e->y);
    if (id) { // выбор + сразу готов к драгу
        g_wtSel = id;
        wt_drag_begin(id, 0, e->x, e->y);
        return 1;
    }
    int inVp = e->x >= g_vpx && e->x < g_vpx + g_vpw && e->y >= g_vpy &&
               e->y < g_vpy + g_vph;
    if (inVp) {
        g_wtSel = 0;
        return 1;
    }
    return 0; // доки/меню
}

// Заливка панелей в vec-ядро (полная, без гизмо/dirty-rect: стрелок нет,
// панели дёшевы). Плейсхолдер вьюпорта — тёмной плашкой поверх пустоты.
static void edit_push(void) {
    const UiFrame *fr = 0;
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = 0;
    int panelsDirty = ui_panels_build(g_panels, &g_scene, g_WW, g_WH, &fr);
    if (fr) {
        for (int i = 0; i < fr->n && n < UI_MAX_PRIMS; i++, n++) {
            shapes[n].kind = fr->prims[i].kind;
            shapes[n].a = fr->prims[i].a;
            shapes[n].b = fr->prims[i].b;
            shapes[n].c = fr->prims[i].c;
            shapes[n].d = fr->prims[i].d;
            shapes[n].e = fr->prims[i].e;
            shapes[n].f = fr->prims[i].f;
            memcpy(shapes[n].rgba, fr->prims[i].rgba, sizeof shapes[n].rgba);
        }
    }
    ui_panels_viewport(g_panels, g_WW, g_WH, &g_vpx, &g_vpy, &g_vpw, &g_vph);
    if (n < UI_MAX_PRIMS && g_vpw > 0 && g_vph > 0) {
        // Плашка вместо 3D (игровой редактор подставит сюда frame_to).
        static const float voidCol[4] = {0.03f, 0.04f, 0.07f, 1.0f};
        shapes[n].kind = 0;
        shapes[n].a = (float)g_vpx;
        shapes[n].b = (float)g_vpy;
        shapes[n].c = (float)g_vpw;
        shapes[n].d = (float)g_vph;
        shapes[n].e = shapes[n].f = 0;
        memcpy(shapes[n].rgba, voidCol, sizeof shapes[n].rgba);
        n++;
    }
    (void)panelsDirty;
    wt_draw_all(shapes, &n);
    pal_draw(shapes, &n);
    if (n && g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
}

static void route_event(const UiEvent *e) {
    // Порядок §14: виджеты/палитра -> доки. DOWN/UP, съеденный холстом,
    // в доки не идёт; MOVE форвардим обоим (ховер и там, и там).
    int mine = wt_event(e);
    if (mine && e->kind != UI_EV_MOVE) {
        edit_push();
        return;
    }
    UiReply r = ui_panels_event(g_panels, &g_scene, e);
    if (mine || r.dirty) edit_push();
}

static void on_mouse(GLFWwindow *win, int button, int action, int mods) {
    (void)win;
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.button = 0;
    e.x = (float)g_mx;
    e.y = (float)g_my;
    if (action == GLFW_PRESS) {
        e.kind = UI_EV_DOWN;
        route_event(&e);
    } else if (action == GLFW_RELEASE) {
        e.kind = UI_EV_UP;
        route_event(&e);
    }
}

static void on_move(GLFWwindow *win, double x, double y) {
    g_mx = x * g_mouseKx;
    g_my = y * g_mouseKy;
    {
        static int lastHov = -1;
        int hov = g_panels ? ui_panels_hover(g_panels, (float)g_mx, (float)g_my) : 0;
        if (wt_handle_hit((float)g_mx, (float)g_my) > 0) hov = 1; // ручка = resize
        if (hov != lastHov) {
            lastHov = hov;
            if (g_curArrow && g_curResize) glfwSetCursor(win, hov ? g_curResize : g_curArrow);
        }
    }
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.kind = UI_EV_MOVE;
    e.x = (float)g_mx;
    e.y = (float)g_my;
    route_event(&e);
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        g_quit = 1;
        return;
    }
    // Инструменту нужны: Tab (коллапс), Z/Y (undo), L (уже в EDIT — no-op).
    // QWER без гизмо бессмысленны — не форвардим.
    if (key == GLFW_KEY_TAB || key == GLFW_KEY_Z || key == GLFW_KEY_Y || key == GLFW_KEY_L) {
        UiEvent e;
        memset(&e, 0, sizeof e);
        e.kind = UI_EV_KEY;
        e.key = key;
        e.mods = mods;
        route_event(&e);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        // Без окна/GL: панели в EDIT строят примитивы, вьюпорт — остаток,
        // скриптованный сплиттер двигает hierW. Файлов не пачкаем (remove).
        int bad = 0;
        // Герметичность: чужую раскладку (layout.json) не читаем и не сносим —
        // прячем на время теста, возвращаем как было (иначе тест врёт и рушит
        // рабочую раскладку пользователя).
        int laySaved = (rename("engine/assets/layout.json",
                               "engine/assets/layout.json.selftest-bak") == 0);
        ScWorld sw;
        sc_init(&sw);
        Entity a = sc_spawn(&sw, "A");
        Entity b = sc_spawn(&sw, "B");
        (void)a;
        (void)b;
        UiPanels *pn = ui_panels_create();
        const UiFrame *pf = 0;
        UiEvent pe;
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_KEY;
        pe.key = 'L';
        if (!ui_panels_event(pn, &sw, &pe).handled || !ui_panels_edit(pn)) {
            printf("SELF FAIL: edit on\n");
            bad = 1;
        }
        if (!ui_panels_build(pn, &sw, W, H, &pf) || !pf || pf->n < 4) {
            printf("SELF FAIL: build\n");
            bad = 1;
        }
        int x, y, w, h;
        ui_panels_viewport(pn, W, H, &x, &y, &w, &h);
        // Остаток после доков (дефолт 220/220; файл может менять — считаем от него).
        if (y != 58 || h != H - 58 - 26 || x + w > W) {
            printf("SELF FAIL: viewport %d %d %d %d\n", x, y, w, h);
            bad = 1;
        }
        int x0 = x;
        // Сплиттер: DOWN на левом краю + MOVE +40 => hierW растёт на 40.
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_DOWN;
        pe.x = (float)(x0 - 3);
        pe.y = 300;
        if (!ui_panels_event(pn, &sw, &pe).handled) {
            printf("SELF FAIL: splitter grab\n");
            bad = 1;
        }
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_MOVE;
        pe.x = (float)(x0 - 3 + 40);
        pe.y = 300;
        ui_panels_event(pn, &sw, &pe);
        memset(&pe, 0, sizeof pe);
        pe.kind = UI_EV_UP;
        ui_panels_event(pn, &sw, &pe);
        ui_panels_viewport(pn, W, H, &x, &y, &w, &h);
        if (x != x0 + 40) {
            printf("SELF FAIL: splitter drag %d want %d\n", x, x0 + 40);
            bad = 1;
        }
        // §14 F2a: палитра + холст (создание/выбор) без окна.
        g_WW = W;
        g_WH = H;
        g_vpx = x;
        g_vpy = y;
        g_vpw = w;
        g_vph = h;
        {
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_palHot = -1;
            memset(g_wtN, 0, sizeof g_wtN);
            UiEvent we;
            float px, py, pw, ph;
            // DOWN по палитре (Panel) -> виджет создан и выбран.
            pal_rect(0, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 5;
            we.y = py + 5;
            if (!wt_event(&we) || g_wt.count != 1 || !g_wtSel ||
                widget_cget(&g_wt, g_wtSel)->type != UI_W_PANEL) {
                printf("SELF FAIL: pal-spawn\n");
                bad = 1;
            }
            int p1 = g_wtSel;
            // Клик по телу виджета -> он же выбран (не сбрасывается).
            const UiWidget *W1 = widget_cget(&g_wt, p1);
            float bx, by, bw, bh;
            widget_rect_px(W1, g_WW, g_WH, &bx, &by, &bw, &bh);
            g_wtSel = 0; // сброс, чтобы проверить именно выбор кликом
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = bx + bw / 2;
            we.y = by + bh / 2;
            if (!wt_event(&we) || g_wtSel != p1) {
                printf("SELF FAIL: wt-select\n");
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we); // отпускание: клик, а не вечный драг
            // Палитра Button -> второй виджет, тип верный.
            pal_rect(1, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 5;
            we.y = py + 5;
            wt_event(&we);
            if (g_wt.count != 2 || !widget_cget(&g_wt, g_wtSel) ||
                widget_cget(&g_wt, g_wtSel)->type != UI_W_BUTTON ||
                widget_cget(&g_wt, p1)->type != UI_W_PANEL) {
                printf("SELF FAIL: pal-spawn2\n");
                bad = 1;
            }
            // Пустой клик по холсту -> снятие выбора (наш, не доки).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = (float)(g_vpx + g_vpw - 5);
            we.y = (float)(g_vpy + g_vph - 5);
            if (!wt_event(&we) || g_wtSel != 0) {
                printf("SELF FAIL: wt-deselect\n");
                bad = 1;
            }
            // Клик по доку (вне вьюпорта) -> не наш (доки не крадём).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = 2;
            we.y = (float)(g_WH / 2);
            if (wt_event(&we)) {
                printf("SELF FAIL: wt-dock-steal\n");
                bad = 1;
            }
            // Ховер палитры: MOVE по кнопке -> подсветка меняется (перерисовка).
            pal_rect(2, &px, &py, &pw, &ph);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = px + 5;
            we.y = py + 5;
            if (!wt_event(&we) || g_palHot != 2) {
                printf("SELF FAIL: pal-hover\n");
                bad = 1;
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
        }
        // §14 F2b: драг перемещения + ресайз (frozen-frame: якорь на DOWN).
        {
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_palHot = -1;
            g_dragId = 0;
            g_dragHandle = 0;
            memset(g_wtN, 0, sizeof g_wtN);
            int id = wt_spawn(UI_W_BUTTON);
            float x0, y0, w0, h0;
            widget_rect_px(widget_cget(&g_wt, id), g_WW, g_WH, &x0, &y0, &w0, &h0);
            UiEvent we;
            // DOWN в теле -> move, MOVE +50/+30 -> сдвиг ровно на это, размер тот же.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x0 + w0 / 2;
            we.y = y0 + h0 / 2;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x0 + w0 / 2 + 50;
            we.y = y0 + h0 / 2 + 30;
            wt_event(&we);
            float x1, y1, w1, h1;
            widget_rect_px(widget_cget(&g_wt, id), g_WW, g_WH, &x1, &y1, &w1, &h1);
            if (x1 != x0 + 50 || y1 != y0 + 30 || w1 != w0 || h1 != h0) {
                printf("SELF FAIL: wt-move %f %f %f %f\n", x1, y1, w1, h1);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            if (g_dragId) {
                printf("SELF FAIL: wt-up\n");
                bad = 1;
            }
            // Ресайз правым краем (хэндл R): +40 -> ширина +40, позиция та же.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x1 + w1 - 1;
            we.y = y1 + h1 / 2;
            wt_event(&we);
            if (!g_dragId || g_dragHandle != 2) {
                printf("SELF FAIL: wt-handle-r %d\n", g_dragHandle);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x1 + w1 - 1 + 40;
            we.y = y1 + h1 / 2;
            wt_event(&we);
            float x2, y2, w2, h2;
            widget_rect_px(widget_cget(&g_wt, id), g_WW, g_WH, &x2, &y2, &w2, &h2);
            if (x2 != x1 || y2 != y1 || w2 != w1 + 40 || h2 != h1) {
                printf("SELF FAIL: wt-resize-r %f\n", w2);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            // Мин-размер: левый край тянем вправо далеко -> ширина клампится в WT_MIN.
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = x2 + 1;
            we.y = y2 + h2 / 2;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = x2 + 1 + w2 + 100;
            we.y = y2 + h2 / 2;
            wt_event(&we);
            float x3, y3, w3, h3;
            widget_rect_px(widget_cget(&g_wt, id), g_WW, g_WH, &x3, &y3, &w3, &h3);
            if (w3 != WT_MIN) {
                printf("SELF FAIL: wt-min %f\n", w3);
                bad = 1;
            }
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_dragId = 0;
            g_dragHandle = 0;
        }
        // §14 F1: модель виджетов — add/find/reparent/remove + rect-якоря +
        // save/load roundtrip в /tmp (следов не оставляем).
        {
            UiWidgetTree wt;
            widget_tree_init(&wt);
            UiWidgetRect rP = {UI_A_LEFT | UI_A_TOP, 10, 20, 200, 100};
            UiWidgetStyle sP = {{0.2f, 0.2f, 0.22f, 1.0f}, 4.0f, 1.0f, 16.0f};
            int P = widget_add(&wt, UI_W_PANEL, "P", 0, &rP, &sP);
            UiWidgetRect rB = {UI_A_LEFT | UI_A_TOP, 8, 8, 80, 28};
            int B = widget_add(&wt, UI_W_BUTTON, "Ok", P, &rB, 0);
            UiWidgetRect rL = {UI_A_RIGHT | UI_A_BOTTOM, 8, 8, 120, 20};
            int L = widget_add(&wt, UI_W_LABEL, "T", P, &rL, 0);
            if (!P || !B || !L || wt.count != 3) {
                printf("SELF FAIL: wadd\n");
                bad = 1;
            }
            if (widget_find(&wt, "Ok") != B || widget_find(&wt, "Nope")) {
                printf("SELF FAIL: wfind\n");
                bad = 1;
            }
            // Якоря: L+T = как есть; R+B = от правого/нижнего края.
            float qx, qy, qw, qh;
            widget_rect_px(widget_cget(&wt, P), W, H, &qx, &qy, &qw, &qh);
            if (qx != 10 || qy != 20 || qw != 200 || qh != 100) {
                printf("SELF FAIL: wrect-lt\n");
                bad = 1;
            }
            widget_rect_px(widget_cget(&wt, L), W, H, &qx, &qy, &qw, &qh);
            if (qx != W - 8 - 120 || qy != H - 8 - 20) {
                printf("SELF FAIL: wrect-rb %f %f\n", qx, qy);
                bad = 1;
            }
            // Reparent L под B; цикл P под L — отказ; чужой id — отказ.
            if (!widget_reparent(&wt, L, B)) {
                printf("SELF FAIL: wreparent\n");
                bad = 1;
            }
            if (widget_reparent(&wt, P, L) || widget_reparent(&wt, B, 999) ||
                widget_reparent(&wt, 999, 0) || widget_reparent(&wt, P, P)) {
                printf("SELF FAIL: wreparent-bad-ok\n");
                bad = 1;
            }
            // Remove B валит поддерево (B+L); P сиротеет, count 1.
            if (!widget_remove(&wt, B) || widget_find(&wt, "T") || wt.count != 1) {
                printf("SELF FAIL: wremove count=%d\n", wt.count);
                bad = 1;
            }
            if (widget_remove(&wt, B)) {
                printf("SELF FAIL: wremove-dead-ok\n");
                bad = 1;
            }
            // Дети: 8 ок, 9-й отказ (P пуст после remove).
            int kidOk = 1;
            for (int i = 0; i < 8; i++) {
                char nm[16];
                snprintf(nm, sizeof nm, "k%d", i);
                if (!widget_add(&wt, UI_W_LABEL, nm, P, 0, 0)) kidOk = 0;
            }
            if (!kidOk || widget_add(&wt, UI_W_LABEL, "k9", P, 0, 0) || wt.count != 9) {
                printf("SELF FAIL: wkids-cap count=%d\n", wt.count);
                bad = 1;
            }
            // Roundtrip: свежий файл + слияние в файл с доками (ключи целы).
            const char *tp = "/tmp/opencode/widget_test.json";
            remove(tp);
            if (!widget_json_save(&wt, tp)) {
                printf("SELF FAIL: wsave-fresh\n");
                bad = 1;
            }
            {
                FILE *df = fopen("/tmp/opencode/widget_dock.json", "wb");
                if (df) {
                    fputs("{\n  \"hierW\": [220],\n  \"swapped\": [0]\n}\n", df);
                    fclose(df);
                }
            }
            if (!widget_json_save(&wt, "/tmp/opencode/widget_dock.json")) {
                printf("SELF FAIL: wsave-merge\n");
                bad = 1;
            }
            {
                FILE *df = fopen("/tmp/opencode/widget_dock.json", "rb");
                char chk[70000];
                size_t cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [220]") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: wsave-dock-kept\n");
                    bad = 1;
                }
            }
            UiWidgetTree rt;
            widget_tree_init(&rt);
            if (!widget_json_load(&rt, "/tmp/opencode/widget_dock.json") || rt.count != 9) {
                printf("SELF FAIL: wload count=%d\n", rt.count);
                bad = 1;
            } else {
                const UiWidget *rp = widget_cget(&rt, P);
                if (!rp || strcmp(rp->name, "P") || rp->nchildren != 8 ||
                    rp->rect.anchors != (UI_A_LEFT | UI_A_TOP) || rp->style.rounding != 4.0f) {
                    printf("SELF FAIL: wload-data\n");
                    bad = 1;
                }
                // Связи пережили roundtrip (k3 ребёнок P).
                int k3 = widget_find(&rt, "k3");
                const UiWidget *rk = widget_cget(&rt, k3);
                if (!k3 || !rk || rk->parent != P) {
                    printf("SELF FAIL: wload-links\n");
                    bad = 1;
                }
            }
            // Чужой файл: дубль id и цикл — отказ, дерево чистое.
            {
                FILE *bf = fopen("/tmp/opencode/widget_bad.json", "wb");
                if (bf) {
                    fputs("{\"widgets\":["
                          "{\"id\":1,\"type\":0,\"name\":\"A\",\"parent\":2},"
                          "{\"id\":2,\"type\":0,\"name\":\"B\",\"parent\":1}]}",
                          bf);
                    fclose(bf);
                }
            }
            UiWidgetTree bt;
            widget_tree_init(&bt);
            if (widget_json_load(&bt, "/tmp/opencode/widget_bad.json") || bt.count != 0) {
                printf("SELF FAIL: wload-cycle-ok count=%d\n", bt.count);
                bad = 1;
            }
            if (widget_json_load(&rt, "/tmp/opencode/nope.json") || rt.count != 9) {
                printf("SELF FAIL: wload-missing\n");
                bad = 1;
            }
            remove(tp);
            remove("/tmp/opencode/widget_dock.json");
            remove("/tmp/opencode/widget_bad.json");
        }
        ui_panels_destroy(pn);
        // Тест мог писать свою раскладку: своё убираем, чужое возвращаем.
        remove("engine/assets/layout.json");
        if (laySaved)
            rename("engine/assets/layout.json.selftest-bak", "engine/assets/layout.json");
        printf(bad ? "SELF FAIL\n" : "edit selftest OK\n");
        return bad;
    }
    if (!glfwInit()) {
        printf("ui_edit: glfw FAIL\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(W, H, "ui_edit: front (§13)", 0, 0);
    if (!win) {
        printf("ui_edit: window FAIL\n");
        return 1;
    }
    RenderCore *vec = rc_create("vec");
    if (!vec || !vec->init(vec, win)) {
        printf("ui_edit: vec FAIL\n");
        return 1;
    }
    g_vec = vec;
    // Демо-сцена для иерархии (та же тройка что в игровом редакторе).
    sc_init(&g_scene);
    {
        Entity scene = sc_spawn(&g_scene, "Scene");
        Entity hero = sc_spawn(&g_scene, "Hero");
        Entity sword = sc_spawn(&g_scene, "Sword");
        (void)hero;
        if (sword && scene) sc_parent(&g_scene, sword, scene);
    }
    g_panels = ui_panels_create();
    if (!g_panels) {
        printf("ui_edit: panels FAIL\n");
        return 1;
    }
    widget_tree_init(&g_wt); // §14: пусто до создания из палитры
    UiText *text = ui_text_init("engine/assets/mono.ttf", 16.0f);
    if (!text) printf("ui_edit: text OFF (rects only)\n");
    // Всегда EDIT (L скриптом — как дефолтный select в ui_test).
    {
        UiEvent ke;
        memset(&ke, 0, sizeof ke);
        ke.kind = UI_EV_KEY;
        ke.key = 'L';
        ui_panels_event(g_panels, &g_scene, &ke);
        printf("ui_edit: edit=%d (want 1)\n", ui_panels_edit(g_panels));
    }
    unsigned prog = rhi_gl_prog("engine/shaders/blit/blit.vert", "engine/shaders/blit/blit.frag");
    unsigned vao = rhi_gl_empty_vao();
    if (!prog || !vao) {
        printf("ui_edit: blit FAIL\n");
        return 1;
    }
    RcTarget t;
    memset(&t, 0, sizeof t);
    RcView v;
    memset(&v, 0, sizeof v);
    v.resW = W;
    v.resH = H;
    glfwSetMouseButtonCallback(win, on_mouse);
    glfwSetCursorPosCallback(win, on_move);
    glfwSetKeyCallback(win, on_key);
    g_curArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_curResize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    glfwShowWindow(win);
    glfwMaximizeWindow(win);
    glfwPollEvents();
    edit_push(); // первая заливка
    // Демо-виджеты для скриншота палитры/холста (UI_EDIT_SEED=1).
    if (getenv("UI_EDIT_SEED")) {
        wt_spawn(UI_W_PANEL);
        wt_spawn(UI_W_BUTTON);
        wt_spawn(UI_W_LABEL);
        edit_push();
    }
    // Доказательство картинкой: UI_EDIT_SHOT=shot.ppm UI_EDIT_SHOT_AT=60.
    const char *shot = getenv("UI_EDIT_SHOT");
    int shotAt = 60;
    {
        const char *sa = getenv("UI_EDIT_SHOT_AT");
        if (sa && sa[0]) shotAt = atoi(sa);
    }
    int frames = 0;
    static const float hintCol[4] = {0.55f, 0.55f, 0.58f, 1.0f};
    static const float voidCol[4] = {0.35f, 0.38f, 0.45f, 1.0f};
    while (!glfwWindowShouldClose(win) && !g_quit) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        {
            int fw = 0, fh = 0, ww = 0, wh = 0;
            glfwGetFramebufferSize(win, &fw, &fh);
            glfwGetWindowSize(win, &ww, &wh);
            if (fw > 0 && fh > 0 && (fw != g_WW || fh != g_WH)) {
                g_WW = fw;
                g_WH = fh;
                if (ww > 0 && wh > 0) {
                    g_mouseKx = (float)fw / (float)ww;
                    g_mouseKy = (float)fh / (float)wh;
                }
                eng_target_free(&t);
                memset(&t, 0, sizeof t);
                RcView vu;
                memset(&vu, 0, sizeof vu);
                vu.resW = g_WW;
                vu.resH = g_WH;
                if (vec->frame) vec->frame(vec, &vu);
                v.resW = g_WW;
                v.resH = g_WH;
                edit_push();
                printf("ui_edit: resize %dx%d\n", g_WW, g_WH);
            }
        }
        if (!eng_target_resize(&t, g_WW, g_WH)) {
            printf("ui_edit: target FAIL\n");
            return 1;
        }
        // Чистим цель сами (W2: решение вызывающего, не ядра).
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glViewport(0, 0, g_WW, g_WH);
        glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        eng_pass_begin(&t, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, 0);
        if (vec->frame_to) vec->frame_to(vec, &v, &t);
        if (text) {
            ui_panels_text(g_panels, &g_scene, text, g_WW, g_WH);
            ui_text_quad(text, (float)(g_vpx + 10), (float)(g_vpy + 8), "VIEWPORT (3D here)",
                         voidCol);
            // Подписи виджетов (button/label) + палитры.
            static const float tcol[4] = {0.85f, 0.87f, 0.92f, 1.0f};
            for (int i = 0; i < UI_MAX_WIDGETS; i++) {
                const UiWidget *w = widget_cget(&g_wt, i + 1);
                if (!w || w->type == UI_W_PANEL) continue;
                float wx, wy, ww, hh;
                widget_rect_px(w, g_WW, g_WH, &wx, &wy, &ww, &hh);
                ui_text_quad(text, wx + 6, wy + (hh - 16) / 2, w->name, tcol);
            }
            for (int i = 0; i < PAL_N; i++) {
                float px, py, pw, ph;
                pal_rect(i, &px, &py, &pw, &ph);
                ui_text_quad(text, px + 6, py + 5, PAL_LABEL[i], tcol);
            }
            ui_text_quad(text, 10.0f, (float)(g_WH - 48),
                         "frontedit: click palette=add | click widget=select | blue edges resize"
                         " | Tab collapse | Esc quit",
                         hintCol);
            glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            glViewport(0, 0, g_WW, g_WH);
            ui_text_flush(text, g_WW, g_WH);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (shot && shot[0] && frames == shotAt) {
            uint8_t *px = (uint8_t *)malloc((size_t)g_WW * g_WH * 3);
            if (px) {
                glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                glReadPixels(0, 0, g_WW, g_WH, GL_RGB, GL_UNSIGNED_BYTE, px);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                FILE *ff = fopen(shot, "wb");
                if (ff) {
                    fprintf(ff, "P6\n%d %d\n255\n", g_WW, g_WH);
                    for (int y = g_WH - 1; y >= 0; y--)
                        fwrite(px + (size_t)y * g_WW * 3, 1, (size_t)g_WW * 3, ff);
                    fclose(ff);
                    printf("ui_edit: shot %s\n", shot);
                }
                free(px);
            }
        }
        eng_pass_end(&t);
        comp_present_gl(prog, vao, &t, g_WW, g_WH);
        glfwSwapBuffers(win);
        glfwPollEvents();
        frames++;
    }
    printf("ui_edit: frames=%d DONE\n", frames);
    if (g_curArrow) glfwDestroyCursor(g_curArrow);
    if (g_curResize) glfwDestroyCursor(g_curResize);
    g_curArrow = g_curResize = 0;
    ui_panels_destroy(g_panels);
    ui_text_destroy(text);
    return 0;
}
