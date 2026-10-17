// ui_edit: отдельный правщик фронта редактора (§13). Своё окно, без 3D:
// панели (UiPanels, всегда EDIT) + плейсхолдер вьюпорта + хинт.
// НЕ режим внутри игрового редактора (там игре — игра, правке — правка).
// Файлы-обменники с игровым редактором: layout.json + theme.json
// (коммиты пишут сами панели: lay_commit / ui_theme_save).
// Запуск из корня репо (шейдеры относительные, как ui_test): ./engine/build/ui_edit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rhi_gl.h" // ПЕРВЫМ: GLFW_INCLUDE_NONE + glad (иначе gl.h-конфликт)
#include "render_api.h"
#include "ui/ui_panels.h" // меню/тулбар/иерархия/статус + EDIT-правка
#include "ui/ui_widget.h" // дерево виджетов конструктора (§14)
#include "ui/ui_widget_runtime.h" // §14 F3c: общий порядок/рендер/хит (игре и редактору)
#include "ui/ui_text.h" // подписи + хинт инструмента
#include "rtarget.h"
#include "comp_gl.h"
#include "comp.h"
#include "rhi_gl.h"

#define W 1280
#define H 720 // дефолт до первого замера
#define UI_LAYOUT_PATH "engine/assets/layout.json" // обменник с игровым редактором

static int g_WW = W, g_WH = H; // живой размер (фреймбуферные пиксели)
static float g_mouseKx = 1.0f, g_mouseKy = 1.0f;
static double g_mx = 0, g_my = 0;
static UiPanels *g_panels = 0;
static RenderCore *g_vec = 0;
static ScWorld g_scene;
static GLFWcursor *g_curArrow = 0, *g_curResize = 0;
static int g_quit = 0;
static float g_fpsEma = 0;   // оверлей: живой FPS (жалоба «со временем деградирует»)
static double g_lastT = 0;
static int g_fullN = 0;      // счётчик полных edit_push (диагноз: full на MOVE = лаг)
// Бенч драга (UI_EDIT_BENCH=rect|full): синтетический драг K шагов, замер FPS.
#define BENCH_K 600
static int g_benchK = BENCH_K; // переопределяется UI_EDIT_BENCH_K
static int g_bench = 0, g_forceFull = 0, g_benchId = 0, g_benchNoEv = 0;
static int g_benchSpawn = 0, g_benchNSpawn = 10; // UI_EDIT_BENCH=spawn: залп спавнов
static int g_noDirty = 0; // UI_EDIT_FULL=1: всегда полный кадр (диагностика шлейфа)
static int g_benchResize = 0;
static int g_benchCorner = 0;
static int g_benchEvents = 1;  // MOVE-событий за кадр (реальная мышь)
static int g_benchNoCoal = 0;  // 1 = без коалесинга (старое поведение)
static float g_benchX = 0, g_benchY = 0;
static double g_benchT0 = 0;
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
#define PAL_PAD 8    // внутренние поля хром-панели
#define PAL_TITLE 22 // строка заголовка панели
#define WT_HANDLE 8.0f // сторона нарисованного квадрата ручки ресайза
static const int PAL_TYPE[PAL_N] = {UI_W_PANEL, UI_W_BUTTON, UI_W_LABEL};
static const char *PAL_LABEL[PAL_N] = {"Panel", "Button", "Label"};

// --- §14 F3c(1b): ХРОМ РЕДАКТОРА = ВИДЖЕТЫ (self-host) ---
// Свой UI правщика живёт ВТОРЫМ деревом (g_edWt): палитра/дерево/инспектор — это
// виджеты, их геометрия берётся отсюда, рисует их ТОТ ЖЕ рантайм, что документ.
// F7 (env UI_EDIT_SELF=1) переключает, ЧТО правим: документ (g_wt, пишется в
// layout.json) или хром самого редактора (g_edWt, не пишется — это вид инструмента).
static UiWidgetTree g_edWt;
static int g_edPal = 0, g_edTree = 0, g_edIns = 0; // id хром-панелей
static int g_edMoved[UI_MAX_WIDGETS]; // 1 = юзер подвинул: дефолтом не перетираем
static int g_editSelf = 0;            // 1 = правим хром редактора
static UiWidgetTree *g_act = &g_wt;   // АКТИВНОЕ дерево (инспектор/драг/дерево)

// Пиксельный rect хром-панели (то же правило якорей, что у виджетов документа).
static void ed_rect(int id, float *x, float *y, float *w, float *h) {
    const UiWidget *p = widget_cget(&g_edWt, id);
    if (!p) {
        *x = *y = *w = *h = 0;
        return;
    }
    widget_rect_px(p, g_WW, g_WH, x, y, w, h);
}

static void ed_chrome_init(void) {
    widget_tree_init(&g_edWt);
    memset(g_edMoved, 0, sizeof g_edMoved);
    UiWidgetStyle s, s2;
    memset(&s, 0, sizeof s);
    s.alpha = 1.0f;
    s.rounding = 6.0f;
    s.fontSize = 14.0f;
    s.bg[0] = 0.11f;
    s.bg[1] = 0.12f;
    s.bg[2] = 0.16f;
    s.bg[3] = 1.0f;
    s2 = s;
    s2.bg[0] = 0.09f;
    s2.bg[1] = 0.10f;
    s2.bg[2] = 0.13f;
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    g_edPal = widget_add(&g_edWt, UI_W_PANEL, "EdPalette", 0, &r, &s);
    g_edTree = widget_add(&g_edWt, UI_W_PANEL, "EdHierarchy", 0, &r, &s2);
    g_edIns = widget_add(&g_edWt, UI_W_PANEL, "EdInspector", 0, &r, &s2);
}

static void pal_rect(int i, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edPal, &px, &py, &pw, &ph);
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + PAL_PAD + PAL_TITLE + (float)(i * (PAL_BH + PAL_GAP));
    *w = pw - 2 * PAL_PAD; // кнопки палитры — во всю ширину панели
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

// Прямоугольник спавна (якорь L|T, в пикселях окна) — ПРАВЕЕ панелей хрома
// (иначе виджет рождается под панелью и «пропадает»), каскадом вниз.
static UiWidgetRect wt_spawn_rect(int type) {
    UiWidgetRect r;
    memset(&r, 0, sizeof r);
    r.anchors = UI_A_LEFT | UI_A_TOP;
    float px, py, pw, ph;
    ed_rect(g_edPal, &px, &py, &pw, &ph);
    r.x = px + pw + 24 + (float)((g_wt.count % 5) * 28);
    r.y = py + 40 + (float)((g_wt.count % 5) * 26);
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

// §14 F3c: порядок слоёв, хит и отрисовка виджетов — из ОБЩЕГО рантайма
// (ui_widget_runtime). Здесь только тонкие обёртки над деревом редактора g_wt,
// чтобы игра и редактор гарантированно видели одинаковый порядок/верх.
static int wt_sib_sorted(int parent, int *out) {
    return ui_widget_sib_sorted(g_act, parent, out);
}

static int wt_hit(float px, float py) {
    return ui_widget_hit(g_act, g_WW, g_WH, px, py);
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

// §14 F3c: заливки виджетов — из ОБЩЕГО рантайма (то же, что увидит игра).
static void wt_draw_all(RcVecShape *sh, int *n) {
    RcVecShape tmp[UI_MAX_PRIMS];
    int m = ui_widget_build(&g_wt, g_WW, g_WH, tmp, UI_MAX_PRIMS);
    for (int i = 0; i < m && *n < UI_MAX_PRIMS; i++) sh[(*n)++] = tmp[i];
}

// Хром редактора — тем же рантаймом (и это же дерево правится в self-режиме).
static void chrome_draw(RcVecShape *sh, int *n) {
    RcVecShape tmp[UI_MAX_PRIMS];
    int m = ui_widget_build(&g_edWt, g_WW, g_WH, tmp, UI_MAX_PRIMS);
    for (int i = 0; i < m && *n < UI_MAX_PRIMS; i++) sh[(*n)++] = tmp[i];
}

// Рамка выбора + ручки — это ХРОМ РЕДАКТОРА, потому локально и ПОВЕРХ всего
// (в self-режиме обводка ложится на панель хрома, а не тонет под ней).
static void wt_draw_sel(RcVecShape *sh, int *n) {
    const UiWidget *s = widget_cget(g_act, g_wtSel);
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
                wt_emit(sh, n, hxs[i] - WT_HANDLE / 2, hys[j] - WT_HANDLE / 2, WT_HANDLE,
                        WT_HANDLE, sel, 0);
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

// --- F3: дерево виджетов (иерархия + выбор кликом) ---
// Строки идут в том же порядке, что и отрисовка (корни -> дети, в глубину).
#define TREE_W 180
#define TREE_RH 20
#define TREE_HDR 22
static int g_treeN = 0;
static int g_treeId[UI_MAX_WIDGETS];
static int g_treeDepth[UI_MAX_WIDGETS];
static int g_treeScroll = 0; // первая видимая строка: панель — окно со скроллом

// Сколько строк влезает в панель дерева (высота — из хром-виджета).
static int tree_rows_fit(void) {
    float px, py, pw, ph;
    ed_rect(g_edTree, &px, &py, &pw, &ph);
    (void)px;
    (void)py;
    (void)pw;
    int rows = (int)((ph - TREE_HDR - PAL_PAD) / TREE_RH);
    return rows < 1 ? 1 : rows;
}

static void tree_collect(int id, int depth) {
    if (g_treeN >= UI_MAX_WIDGETS) return;
    const UiWidget *w = widget_cget(g_act, id);
    if (!w) return;
    g_treeId[g_treeN] = id;
    g_treeDepth[g_treeN] = depth;
    g_treeN++;
    int kids[UI_MAX_WIDGETS];
    int nk = wt_sib_sorted(id, kids); // порядок = слои (как рисуем)
    for (int i = 0; i < nk; i++) tree_collect(kids[i], depth + 1);
}
static void tree_build(void) {
    g_treeN = 0;
    int roots[UI_MAX_WIDGETS];
    int nr = wt_sib_sorted(0, roots);
    for (int i = 0; i < nr; i++) tree_collect(roots[i], 0);
    // Прокрутка: выбранного держим в кадре, за края списка не вылезаем.
    int rows = tree_rows_fit();
    for (int i = 0; i < g_treeN; i++)
        if (g_treeId[i] == g_wtSel) {
            if (i < g_treeScroll) g_treeScroll = i;
            if (i >= g_treeScroll + rows) g_treeScroll = i - rows + 1;
        }
    int maxs = g_treeN - rows;
    if (maxs < 0) maxs = 0;
    if (g_treeScroll > maxs) g_treeScroll = maxs;
    if (g_treeScroll < 0) g_treeScroll = 0;
}
// row — индекс ВИДИМОЙ строки (0..rows-1): ширина = внутренняя ширина панели
// минус место под индикатор прокрутки, так строки не вылезают за хром-виджет.
static void tree_rect(int row, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edTree, &px, &py, &pw, &ph);
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + TREE_HDR + (float)(row * TREE_RH);
    *w = pw - 2 * PAL_PAD - 4;
    *h = (float)TREE_RH;
}
static int tree_hit(float px, float py) {
    int rows = tree_rows_fit();
    for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
        float x, y, w, h;
        tree_rect(i - g_treeScroll, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return g_treeId[i];
    }
    return 0;
}
static void type_col(int type, float out[4]) {
    if (type == UI_W_BUTTON) { out[0] = 0.35f; out[1] = 0.65f; out[2] = 0.90f; }
    else if (type == UI_W_LABEL) { out[0] = 0.80f; out[1] = 0.75f; out[2] = 0.40f; }
    else { out[0] = 0.55f; out[1] = 0.60f; out[2] = 0.70f; }
    out[3] = 1.0f;
}
static void tree_draw(RcVecShape *sh, int *n) {
    tree_build();
    int rows = tree_rows_fit();
    for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
        float x, y, w, h;
        tree_rect(i - g_treeScroll, &x, &y, &w, &h);
        const UiWidget *wd = widget_cget(g_act, g_treeId[i]);
        if (!wd) continue;
        int sel = g_treeId[i] == g_wtSel;
        float c[4] = {0.15f, 0.16f, 0.20f, 1.0f};
        if (sel) { c[0] = 0.22f; c[1] = 0.34f; c[2] = 0.62f; }
        wt_emit(sh, n, x, y, w, h, c, 3.0f);
        float tc[4];
        type_col(wd->type, tc);
        wt_emit(sh, n, x + 4 + (float)(g_treeDepth[i] * 10), y + 4, 4, h - 8, tc, 0);
    }
    // Индикатор прокрутки (UE/Blender): сразу видно, что список — окно и где мы
    // в нём. Без него «не листается» неотличимо от «листать нечего».
    if (g_treeN > rows) {
        float px, py, pw, ph;
        ed_rect(g_edTree, &px, &py, &pw, &ph);
        float track[4] = {0.16f, 0.18f, 0.22f, 0.85f};
        float thumb[4] = {0.45f, 0.52f, 0.62f, 1.0f};
        float barW = 3.0f;
        float bx = px + pw - PAL_PAD + 1.0f;
        float ty = py + TREE_HDR;
        float thh = ph - TREE_HDR - PAL_PAD;
        int maxs = g_treeN - rows;
        float frac = maxs > 0 ? (float)g_treeScroll / (float)maxs : 0.0f;
        float bh = thh * (float)rows / (float)g_treeN;
        if (bh < 14.0f) bh = 14.0f;
        float by = ty + (thh - bh) * (frac < 0 ? 0 : frac > 1 ? 1 : frac);
        wt_emit(sh, n, bx, ty, barW, thh, track, 1.5f);
        wt_emit(sh, n, bx, by, barW, bh, thumb, 1.5f);
    }
}

// --- F3b: инспектор (правка выбранного) ---
// Справа: строки полей. Числа тянутся мышью влево/вправо (scrub, как в
// Blender/UE); Lay +/- меняет слой среди соседей. Пишем на коммит (UP).
#define INS_W 200
#define INS_RH 20
#define INS_GAP 2
#define INS_HEAD 20 // строка заголовка (имя выбранного)
enum {
    IF_X, IF_Y, IF_W, IF_H, IF_ROUND, IF_ALPHA, IF_FONT,
    IF_BG_R, IF_BG_G, IF_BG_B, IF_UP, IF_DOWN, IF_N
};
static const char *INS_LABEL[IF_N] = {"x",   "y",     "w",     "h",     "round",
                                      "alpha", "font", "bg.r", "bg.g", "bg.b",
                                      "Lay +", "Lay -"};
static int g_insField = -1;      // какой ряд тянем (scrub), -1 = нет
static float g_insDownX = 0;     // мышь на DOWN
static float g_insDownVal = 0;   // значение на DOWN
static int g_insPrevPx[4];       // прошлый пиксельный rect (dirty-rect)

static void ins_rect(int i, float *x, float *y, float *w, float *h) {
    float px, py, pw, ph;
    ed_rect(g_edIns, &px, &py, &pw, &ph);
    (void)pw;
    (void)ph;
    *x = px + PAL_PAD;
    *y = py + PAL_PAD + (i < 0 ? 0.0f : INS_HEAD + (float)(i * (INS_RH + INS_GAP)));
    *w = (float)INS_W;
    *h = (i < 0 ? (float)(INS_HEAD - 4) : (float)INS_RH);
}

// Раскладка хрома по умолчанию: панели привязаны к вьюпорту (L|T-якоря и оффсеты
// окна) — двинулся док, двинулся хром. Правку юзера (g_edMoved) не перетираем.
static void ed_chrome_sync(void) {
    float palW = (float)((TREE_W > PAL_BW ? TREE_W : PAL_BW) + 2 * PAL_PAD);
    float palH = (float)(PAL_PAD * 2 + PAL_TITLE + PAL_N * (PAL_BH + PAL_GAP) - PAL_GAP);
    float x0 = (float)(g_vpx + PAL_PAD), y0 = (float)(g_vpy + PAL_PAD);
    struct {
        int id;
        float x, y, w, h;
    } d[3];
    d[0].id = g_edPal;
    d[0].x = x0;
    d[0].y = y0;
    d[0].w = palW;
    d[0].h = palH;
    d[1].id = g_edTree;
    d[1].x = x0;
    d[1].y = y0 + palH + 10;
    d[1].w = palW;
    d[1].h = (float)(PAL_PAD * 2 + TREE_HDR + TREE_RH * 10); // ~10 строк, дальше скролл
    { // не вылезаем за низ окна
        float avail = (float)(g_vpy + g_vph) - (y0 + palH + 10) - PAL_PAD;
        float one = (float)(PAL_PAD * 2 + TREE_HDR + TREE_RH);
        if (d[1].h > avail) d[1].h = avail;
        if (d[1].h < one) d[1].h = one;
    }
    d[2].id = g_edIns;
    d[2].w = (float)(INS_W + 2 * PAL_PAD);
    d[2].x = (float)(g_vpx + g_vpw - INS_W - 2 * PAL_PAD);
    d[2].y = y0;
    d[2].h = (float)(PAL_PAD * 2 + INS_HEAD + IF_N * (INS_RH + INS_GAP));
    for (int i = 0; i < 3; i++) {
        UiWidget *w = widget_get(&g_edWt, d[i].id);
        if (!w || g_edMoved[d[i].id - 1]) continue;
        w->rect.anchors = UI_A_LEFT | UI_A_TOP;
        w->rect.x = d[i].x;
        w->rect.y = d[i].y;
        w->rect.w = d[i].w;
        w->rect.h = d[i].h;
    }
}
static float *ins_slot(UiWidget *w, int f) {
    switch (f) {
    case IF_X: return &w->rect.x;
    case IF_Y: return &w->rect.y;
    case IF_W: return &w->rect.w;
    case IF_H: return &w->rect.h;
    case IF_ROUND: return &w->style.rounding;
    case IF_ALPHA: return &w->style.alpha;
    case IF_FONT: return &w->style.fontSize;
    case IF_BG_R: return &w->style.bg[0];
    case IF_BG_G: return &w->style.bg[1];
    case IF_BG_B: return &w->style.bg[2];
    }
    return 0;
}
static float ins_step(int f) {
    if (f == IF_ROUND) return 0.25f;
    if (f == IF_ALPHA || (f >= IF_BG_R && f <= IF_BG_B)) return 0.01f;
    return 1.0f;
}
static void ins_clamp(int f, float *v) {
    if (f == IF_W || f == IF_H) {
        if (*v < 8.0f) *v = 8.0f;
    } else if (f == IF_ROUND) {
        if (*v < 0) *v = 0;
    } else if (f == IF_ALPHA || (f >= IF_BG_R && f <= IF_BG_B)) {
        if (*v < 0) *v = 0;
        if (*v > 1) *v = 1;
    } else if (f == IF_FONT) {
        if (*v < 6) *v = 6;
        if (*v > 48) *v = 48;
    }
}
static int ins_hit(float px, float py) {
    if (!g_wtSel) return -1;
    for (int i = 0; i < IF_N; i++) {
        float x, y, w, h;
        ins_rect(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) return i;
    }
    return -1;
}
static void ins_px(int *out) {
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    if (!w) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    float x, y, ww, hh;
    widget_rect_px(w, g_WW, g_WH, &x, &y, &ww, &hh);
    out[0] = (int)x;
    out[1] = (int)y;
    out[2] = (int)(ww + 0.5f);
    out[3] = (int)(hh + 0.5f);
}
static void ins_draw(RcVecShape *sh, int *n) {
    if (!g_wtSel) return;
    const UiWidget *w = widget_cget(g_act, g_wtSel);
    if (!w) return;
    float tx, ty, tw, th;
    ins_rect(-1, &tx, &ty, &tw, &th); // заголовок внутри панели хрома
    float hc[4] = {0.22f, 0.24f, 0.30f, 1.0f};
    wt_emit(sh, n, tx, ty, tw, th, hc, 3.0f);
    for (int i = 0; i < IF_N; i++) {
        float x, y, ww, hh;
        ins_rect(i, &x, &y, &ww, &hh);
        float c[4] = {0.16f, 0.17f, 0.21f, 1.0f};
        if (i == g_insField) { c[0] = 0.30f; c[1] = 0.45f; c[2] = 0.75f; }
        wt_emit(sh, n, x, y, ww, hh, c, 3.0f);
        if (i >= IF_BG_R && i <= IF_BG_B) {
            float sc[4] = {w->style.bg[0], w->style.bg[1], w->style.bg[2], 1.0f};
            wt_emit(sh, n, x + ww - 18, y + 3, 14, hh - 6, sc, 2.0f);
        }
    }
}

// --- F2b: драг/ресайз (frozen-frame как гизмо: якорь на DOWN) ---
// Хэндл = битмаска краёв: L=1 R=2 T=4 B=8; 0 = перемещение.
static int g_dragId = 0;
static int g_dragHandle = 0;
static float g_dragMX = 0, g_dragMY = 0;             // мышь на DOWN (px окна)
static float g_dragX = 0, g_dragY = 0, g_dragW = 0, g_dragH = 0; // rect на DOWN
static float g_prevX = 0, g_prevY = 0, g_prevW = 0, g_prevH = 0; // прошлый шаг
// Dirty-регионы шага драга: 1 = есть что заливать. Список до 8 rect (x,y,w,h).
// Перемещение -> старый+новый; ресайз -> полосы дельты (что реально изменилось),
// а не весь виджет — иначе цена растёт вместе с размером (это и был «лаг»).
static int g_part = 0;
static int g_dirtyR[8][4];
static int g_dirtyN = 0;

#define WT_GRAB 9.0f
#define WT_MIN 8.0f

// Хэндл ресайза под точкой (у выбранного). 0 = не хэндл (тело/мимо).
static int wt_handle_hit(float px, float py) {
    const UiWidget *s = widget_cget(g_act, g_wtSel);
    if (!s) return 0;
    float x, y, w, h;
    widget_rect_px(s, g_WW, g_WH, &x, &y, &w, &h);
    if (px < x - 3 || px > x + w + 3 || py < y - 3 || py > y + h + 3) return 0;
    // Малый виджет: полоса захвата не больше трети стороны — иначе весь
    // прямоугольник станет «ручкой» и тело не подвинуть (баг «ресайз вместо move»).
    float gx = WT_GRAB, gy = WT_GRAB;
    if (w < 3 * WT_GRAB) gx = w / 3;
    if (h < 3 * WT_GRAB) gy = h / 3;
    int hb = 0;
    if (px <= x + gx) hb |= 1;
    if (px >= x + w - gx) hb |= 2;
    if (py <= y + gy) hb |= 4;
    if (py >= y + h - gy) hb |= 8;
    return hb;
}

static void wt_drag_begin(int id, int handle, float mx, float my) {
    const UiWidget *w = widget_cget(g_act, id);
    if (!w) return;
    if (g_editSelf) g_edMoved[id - 1] = 1; // правка юзера: раскладка больше не вернёт
    g_dragId = id;
    g_dragHandle = handle;
    g_dragMX = mx;
    g_dragMY = my;
    widget_rect_px(w, g_WW, g_WH, &g_dragX, &g_dragY, &g_dragW, &g_dragH);
    g_prevX = g_dragX;
    g_prevY = g_dragY;
    g_prevW = g_dragW;
    g_prevH = g_dragH;
    g_part = 0;
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
    UiWidget *w = widget_get(g_act, g_dragId);
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
    // Dirty-регионы = СИММЕТРИЧЕСКАЯ РАЗНОСТЬ старого и нового rect (полосы по
    // сдвинутым краям) + срединные ручки, съезжающие ВНУТРИ неизменной середины.
    // Цена ~ ДЕЛЬТЕ драга, не размеру виджета. Единый путь для move и resize:
    // move = поехали все 4 края, resize = один-два. Если дельта огромная (флин),
    // полосы дороже — откат на объединение/два rect'а (честный выбор по площади).
    {
        const float M = 12.0f;
        float oL = g_prevX, oT = g_prevY, oR = g_prevX + g_prevW, oB = g_prevY + g_prevH;
        float nL = nx, nT = ny, nR = nx + nw, nB = ny + nh;
        float uL = oL < nL ? oL : nL, uT = oT < nT ? oT : nT;
        float uR = oR > nR ? oR : nR, uB = oB > nB ? oB : nB;
        float uh = uB - uT, uw = uR - uL;
        float dL = fabsf(oL - nL), dR = fabsf(oR - nR);
        float dT = fabsf(oT - nT), dB = fabsf(oB - nB);
        float oCx = (oL + oR) * 0.5f, nCx = (nL + nR) * 0.5f;
        float oCy = (oT + oB) * 0.5f, nCy = (nT + nB) * 0.5f;
        float stripA = (dL + dR) * (uh + 2 * M) + (dT + dB) * (uw + 2 * M);
        float twoA = (g_prevW + 2 * M) * (g_prevH + 2 * M) + (nw + 2 * M) * (nh + 2 * M);
        float uniA = (uw + 2 * M) * (uh + 2 * M);
        g_dirtyN = 0;
#define WT_ADD_RECT(X, Y, W, H)                                                        \
    do {                                                                               \
        if (g_dirtyN < 12) {                                                           \
            g_dirtyR[g_dirtyN][0] = (int)(X);                                          \
            g_dirtyR[g_dirtyN][1] = (int)(Y);                                          \
            g_dirtyR[g_dirtyN][2] = (int)(W);                                          \
            g_dirtyR[g_dirtyN][3] = (int)(H);                                          \
            g_dirtyN++;                                                                \
        }                                                                              \
    } while (0)
        if (stripA <= twoA && stripA <= uniA) { // основной путь: полосы дельты
            if (dL > 0.5f) WT_ADD_RECT(uL - M, uT - M, dL + 2 * M, uh + 2 * M);
            if (dR > 0.5f) WT_ADD_RECT(uR - dR - M, uT - M, dR + 2 * M, uh + 2 * M);
            if (dT > 0.5f) WT_ADD_RECT(uL - M, uT - M, uw + 2 * M, dT + 2 * M);
            if (dB > 0.5f) WT_ADD_RECT(uL - M, uB - dB - M, uw + 2 * M, dB + 2 * M);
            if (fabsf(nCx - oCx) > 0.5f) { // верх/низ срединные ручки съехали по X
                float cx = (oCx < nCx ? oCx : nCx) - WT_HANDLE;
                float cw = fabsf(nCx - oCx) + 2 * WT_HANDLE;
                WT_ADD_RECT(cx, uT - M, cw, dT + 2 * M);
                WT_ADD_RECT(cx, uB - dB - M, cw, dB + 2 * M);
            }
            if (fabsf(nCy - oCy) > 0.5f) { // лево/право срединные ручки съехали по Y
                float cy = (oCy < nCy ? oCy : nCy) - WT_HANDLE;
                float ch = fabsf(nCy - oCy) + 2 * WT_HANDLE;
                WT_ADD_RECT(uL - M, cy, dL + 2 * M, ch);
                WT_ADD_RECT(uR - dR - M, cy, dR + 2 * M, ch);
            }
            if (g_dirtyN == 0) // ничего не сдвинулось (клик без движения)
                WT_ADD_RECT(nL - M, nT - M, nw + 2 * M, nh + 2 * M);
        } else if (uniA <= twoA) { // флин: объединение одним rect'ом
            WT_ADD_RECT(uL - M, uT - M, uw + 2 * M, uh + 2 * M);
        } else { // флин: два rect'а (старый+новый)
            WT_ADD_RECT(oL - M, oT - M, g_prevW + 2 * M, g_prevH + 2 * M);
            WT_ADD_RECT(nL - M, nT - M, nw + 2 * M, nh + 2 * M);
        }
#undef WT_ADD_RECT
        g_part = 1;
        g_prevX = nx;
        g_prevY = ny;
        g_prevW = nw;
        g_prevH = nh;
    }
    pt_to_anchor(w, nx, ny, nw, nh, g_WW, g_WH);
}

static void edit_push(void); // определения ниже: инспектор зовёт их из wt_event
static void edit_push_dirtyN(const int (*rects)[4], int cnt);

// Персист §14 (F2a2): пишем виджеты в ТОТ ЖЕ layout.json, что и доки (§11).// widget_json_save заменяет только свою секцию, доки не трогает — и наоборот
// (lay_save переносит "widgets"). Пишем на КОММИТ (конец драга/спавн), не на
// каждый шаг. В seed/bench/скриншот-режимах файл не пачкаем (герметичность).
// Куда персист виджетов пишет/читает. В бенчах/сид-режимах — НИКОГДА в документ
// пользователя: уводим в отдельный файл (герметичность тестов). UI_EDIT_PERSIST=1
// оставлен, чтобы мерить ЧЕСТНО (вместе с записью файла), но не по документу.
static const char *wt_path(void) {
    if (getenv("UI_EDIT_BENCH") || getenv("UI_EDIT_SEED"))
        return "engine/assets/layout.bench.json";
    return UI_LAYOUT_PATH;
}
static int wt_persist_ok(void) {
    // UI_EDIT_PERSIST=1 — меряем ЧЕСТНО, с записью файла (бенч залпа спавнов).
    const char *fp = getenv("UI_EDIT_PERSIST");
    if (fp && fp[0] == '1') return 1;
    return !getenv("UI_EDIT_SEED") && !getenv("UI_EDIT_BENCH");
}
static void wt_persist(void) {
    if (g_editSelf) return; // хром редактора — вид инструмента, в layout.json не пишем
    if (wt_persist_ok()) widget_json_save(&g_wt, wt_path());
}

// Событие в холст/палитру. 1 = наше (доки не трогаем). Спавн/выбор/драг — тут.
static int wt_event(const UiEvent *e) {
    if (e->kind == UI_EV_WHEEL) { // скролл списка над панелью дерева
        float px, py, pw, ph;
        ed_rect(g_edTree, &px, &py, &pw, &ph);
        if (e->x >= px && e->x < px + pw && e->y >= py && e->y < py + ph) {
            int step = e->value > 0 ? -3 : e->value < 0 ? 3 : 0;
            g_treeScroll += step;
            tree_build(); // клампы: по длине списка и видимости выбранного
            edit_push();
            return 1;
        }
        return 0;
    }
    if (e->kind == UI_EV_MOVE) {
        if (g_insField >= 0) { // F3b: scrub поля инспектора (правит АКТИВНОЕ дерево)
            UiWidget *w = widget_get(g_act, g_wtSel);
            float *slot = w ? ins_slot(w, g_insField) : 0;
            if (slot) {
                float v = g_insDownVal + (e->x - g_insDownX) * ins_step(g_insField);
                ins_clamp(g_insField, &v);
                *slot = v;
                int now[4], u[4];
                ins_px(now);
                u[0] = g_insPrevPx[0] < now[0] ? g_insPrevPx[0] : now[0];
                u[1] = g_insPrevPx[1] < now[1] ? g_insPrevPx[1] : now[1];
                int rx = g_insPrevPx[0] + g_insPrevPx[2] > now[0] + now[2]
                             ? g_insPrevPx[0] + g_insPrevPx[2]
                             : now[0] + now[2];
                int ry = g_insPrevPx[1] + g_insPrevPx[3] > now[1] + now[3]
                             ? g_insPrevPx[1] + g_insPrevPx[3]
                             : now[1] + now[3];
                u[2] = rx - u[0];
                u[3] = ry - u[1];
                int one[1][4] = {{u[0], u[1], u[2], u[3]}};
                edit_push_dirtyN(one, 1); // растр только региона
                for (int k = 0; k < 4; k++) g_insPrevPx[k] = now[k];
            }
            return 1;
        }
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
        if (g_insField >= 0) { // F3b: конец scrub
            g_insField = -1;
            wt_persist();
            edit_push(); // снять подсветку ряда
            return 1;
        }
        if (g_dragId) {
            g_dragId = 0;
            g_dragHandle = 0;
            wt_persist(); // коммит: конец перемещения/ресайза
            return 1;
        }
        return 0;
    }
    if (e->kind != UI_EV_DOWN) return 0;
    // §14 F3c: правим ХРОМ редактора — палитра/дерево/инспектор не работают
    // (это инструмент документа), клик/драг идёт по панелям хрома.
    if (g_editSelf) {
        int sh = wt_handle_hit(e->x, e->y);
        if (sh > 0 && g_wtSel) {
            wt_drag_begin(g_wtSel, sh, e->x, e->y);
            return 1;
        }
        int cid = wt_hit(e->x, e->y);
        if (cid) {
            g_wtSel = cid;
            wt_drag_begin(cid, 0, e->x, e->y);
            return 1;
        }
        if (e->x >= g_vpx && e->x < g_vpx + g_vpw && e->y >= g_vpy &&
            e->y < g_vpy + g_vph) {
            g_wtSel = 0;
            return 1;
        }
        return 0;
    }
    int pi = pal_hit(e->x, e->y);
    if (pi >= 0) { // палитра поверх всего
        wt_spawn(PAL_TYPE[pi]);
        wt_persist(); // коммит: новый виджет
        return 1;
    }
    tree_build();
    int ti = tree_hit(e->x, e->y);
    if (ti) { // F3: выбор виджета из дерева
        g_wtSel = ti;
        return 1;
    }
    int ii = ins_hit(e->x, e->y);
    if (ii >= 0) { // F3b: инспектор
        if (ii == IF_UP || ii == IF_DOWN) {
            if (widget_layer(g_act, g_wtSel, ii == IF_UP ? 1 : -1)) {
                wt_persist();
                edit_push(); // порядок слоёв изменился — полный кадр
            }
        } else {
            UiWidget *w = widget_get(g_act, g_wtSel);
            float *slot = w ? ins_slot(w, ii) : 0;
            if (slot) {
                g_insField = ii;
                g_insDownX = e->x;
                g_insDownVal = *slot;
                ins_px(g_insPrevPx);
                edit_push(); // подсветка ряда
            }
        }
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
// Собрать кадр примитивов (панели -> плейсхолдер -> виджеты -> палитра).
static int build_shapes(RcVecShape *shapes) {
    const UiFrame *fr = 0;
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
    ed_chrome_sync(); // §14 F3c: раскладка панелей хрома (док мог поменяться)
    wt_draw_all(shapes, &n); // документ — тем же рантаймом, что и игра
    chrome_draw(shapes, &n); // хром редактора — это тоже виджеты
    tree_draw(shapes, &n);   // F3: дерево (под палитрой)
    ins_draw(shapes, &n);    // F3b: инспектор (справа)
    pal_draw(shapes, &n);
    wt_draw_sel(shapes, &n); // рамка/ручки активного — поверх всего
    return n;
}

static void edit_push(void) {
    if (!g_vec) return; // selftest: без GL
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = build_shapes(shapes);
    g_fullN++;
    if (n && g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
}

// Частичная перерисовка (dirty-rect, как в игровом редакторе): растр только
// региона (x,y,w,h, y-вниз), остальное не трогаем. Это и есть лечение лага.
static void edit_push_dirtyN(const int (*rects)[4], int cnt) {
    if (!g_vec) return; // selftest: без GL
    if (cnt <= 0) return;
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = build_shapes(shapes); // один сбор кадра на все регионы
    if (!n) return;
    if (!g_vec->upload_vector_rect) {
        if (g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
        return;
    }
    for (int i = 0; i < cnt; i++) {
        int x = rects[i][0], y = rects[i][1], w = rects[i][2], h = rects[i][3];
        if (w <= 0 || h <= 0) continue;
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > g_WW) w = g_WW - x;
        if (y + h > g_WH) h = g_WH - y;
        if (w <= 0 || h <= 0) continue;
        g_vec->upload_vector_rect(g_vec, shapes, n, x, y, w, h);
    }
}

static void route_event(const UiEvent *e) {
    // Порядок §14: виджеты/палитра -> доки. DOWN/UP, съеденный холстом,
    // в доки не идёт; MOVE форвардим обоим (ховер и там, и там).
    g_part = 0;
    int mine = wt_event(e);
    if (mine && e->kind != UI_EV_MOVE) {
        edit_push();
        return;
    }
    if (g_part && !g_forceFull && !g_noDirty) { // шаг драга: только регионы (dirty-rect)
        edit_push_dirtyN(g_dirtyR, g_dirtyN);
        return;
    }
    UiReply r = ui_panels_event(g_panels, &g_scene, e);
    if (mine || r.dirty) edit_push();
}

// Коалесинг ввода (как UE/Unity): реальная мышь шлёт десятки MOVE-событий за
// кадр, а каждый MOVE у нас = отдельная частичная заливка. Копим события и
// применяем РАЗ ЗА КАДР; подряд идущие MOVE схлопываем в последнюю позицию.
// Без этого лаг зависит от частоты опроса мыши (синтетика = 1 событие/кадр).
#define UI_EVQ 1024
static UiEvent g_evq[UI_EVQ];
static int g_evn = 0;
static void push_ev(const UiEvent *e) {
    if (e->kind == UI_EV_MOVE && g_evn > 0 && g_evq[g_evn - 1].kind == UI_EV_MOVE) {
        g_evq[g_evn - 1] = *e; // схлопнуть подряд идущие MOVE
        return;
    }
    if (g_evn < UI_EVQ) {
        g_evq[g_evn++] = *e;
    } else if (e->kind == UI_EV_MOVE) {
        g_evq[UI_EVQ - 1] = *e; // переполнение: последнюю позицию не теряем
    }
}
static void drain_ev(void) {
    for (int i = 0; i < g_evn; i++) route_event(&g_evq[i]);
    g_evn = 0;
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
        push_ev(&e);
    } else if (action == GLFW_RELEASE) {
        e.kind = UI_EV_UP;
        push_ev(&e);
    }
}

static void on_scroll(GLFWwindow *win, double dx, double dy) {
    (void)win;
    (void)dx;
    UiEvent e;
    memset(&e, 0, sizeof e);
    e.kind = UI_EV_WHEEL;
    e.x = (float)g_mx;
    e.y = (float)g_my;
    e.value = (float)dy;
    push_ev(&e);
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
    push_ev(&e);
}

// §14 F3c: переключить, ЧТО правим — документ или собственный UI редактора.
static void ed_self_toggle(int on) {
    g_editSelf = on ? 1 : 0;
    g_act = g_editSelf ? &g_edWt : &g_wt;
    g_wtSel = 0;
    g_insField = -1;
    g_dragId = 0;
    g_dragHandle = 0;
    g_palHot = -1;
    edit_push();
    printf("ui_edit: self=%d (правим %s)\n", g_editSelf,
           g_editSelf ? "ХРОМ редактора" : "документ (layout.json)");
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) {
        g_quit = 1;
        return;
    }
    if (key == GLFW_KEY_F7) { // self-host: правим собственный UI редактора
        ed_self_toggle(!g_editSelf);
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
        push_ev(&e);
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
        ed_chrome_init(); // §14 F3c(1b): хром = виджеты; геометрия от вьюпорта
        ed_chrome_sync();
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
            // Обратная сторона F2a2: доки (lay_save) НЕ стирают секцию widgets,
            // в обоих порядках записи (read-modify-write чужой секции).
            {
                UiPanels *pp = ui_panels_create();
                const char *mp = "/tmp/opencode/widget_merge2.json";
                remove(mp);
                if (!widget_json_save(&wt, mp) || !ui_panels_save_layout(pp, mp)) {
                    printf("SELF FAIL: lay-save\n");
                    bad = 1;
                }
                FILE *df = fopen(mp, "rb");
                char chk[70000];
                size_t cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: lay-keeps-widgets\n");
                    bad = 1;
                }
                if (!widget_json_save(&wt, mp)) { // обратный порядок
                    printf("SELF FAIL: lay-wsave\n");
                    bad = 1;
                }
                df = fopen(mp, "rb");
                cn = df ? fread(chk, 1, sizeof chk - 1, df) : 0;
                if (df) fclose(df);
                chk[cn] = 0;
                if (!strstr(chk, "\"hierW\": [") || !strstr(chk, "\"widgets\": [")) {
                    printf("SELF FAIL: wsave-keeps-dock2\n");
                    bad = 1;
                }
                UiWidgetTree rt2;
                widget_tree_init(&rt2);
                if (!widget_json_load(&rt2, mp) || rt2.count != 9) {
                    printf("SELF FAIL: lay-merge-load count=%d\n", rt2.count);
                    bad = 1;
                }
                ui_panels_destroy(pp);
                remove(mp);
            }
            // F3: порядок/глубина дерева (корни -> дети, в глубину).
            {
                UiWidgetTree keep = g_wt;
                g_wt = wt; // локальное дерево: P + 8 детей
                tree_build();
                if (g_treeN != 9 || g_treeDepth[0] != 0 || g_treeId[0] != P) {
                    printf("SELF FAIL: tree-order n=%d\n", g_treeN);
                    bad = 1;
                }
                for (int i = 1; i < g_treeN; i++)
                    if (g_treeDepth[i] != 1) {
                        printf("SELF FAIL: tree-depth i=%d\n", i);
                        bad = 1;
                    }
                // Клик по строке = выбор этого виджета (hit по центру строки 1).
                float rx, ry, rw, rh;
                tree_rect(1, &rx, &ry, &rw, &rh);
                if (tree_hit(rx + rw * 0.5f, ry + rh * 0.5f) != g_treeId[1]) {
                    printf("SELF FAIL: tree-hit\n");
                    bad = 1;
                }
                g_wt = keep;
            }
            // F3: слои (z) — сдвиг меняет порядок отрисовки/хита; край не двигается.
            {
                UiWidgetTree keep = g_wt;
                g_wt = wt;
                int before[UI_MAX_WIDGETS], after[UI_MAX_WIDGETS];
                int nb = wt_sib_sorted(P, before);
                if (nb < 2) { printf("SELF FAIL: lay-n\n"); bad = 1; }
                if (!widget_layer(&g_wt, before[0], +1)) {
                    printf("SELF FAIL: lay-raise\n");
                    bad = 1;
                }
                int na = wt_sib_sorted(P, after);
                if (na != nb || after[0] != before[1] || after[1] != before[0]) {
                    printf("SELF FAIL: lay-swap\n");
                    bad = 1;
                }
                if (widget_layer(&g_wt, after[na - 1], +1)) {
                    printf("SELF FAIL: lay-top\n"); // верхний край — некуда
                    bad = 1;
                }
                g_wt = keep;
            }
            // z переживает roundtrip save/load.
            {
                const char *zp = "/tmp/opencode/widget_z.json";
                remove(zp);
                if (!widget_json_save(&wt, zp)) {
                    printf("SELF FAIL: z-save\n");
                    bad = 1;
                }
                UiWidgetTree zt;
                widget_tree_init(&zt);
                if (!widget_json_load(&zt, zp) || zt.count != 9) {
                    printf("SELF FAIL: z-load count=%d\n", zt.count);
                    bad = 1;
                } else {
                    int a = widget_find(&zt, "k1");
                    if (!a || widget_cget(&zt, a)->z != widget_cget(&wt, a)->z) {
                        printf("SELF FAIL: z-keep\n");
                        bad = 1;
                    }
                }
                remove(zp);
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
        // §14 F3c: ОБЩИЙ рантайм — сборка примитивов и верхний хит (то же,
        // что увидит игра). Порядок (z,id) един для сборки, хита и дерева.
        {
            UiWidgetTree srn;
            widget_tree_init(&srn);
            UiWidgetStyle sst;
            memset(&sst, 0, sizeof sst);
            sst.bg[0] = 1.0f;
            sst.bg[3] = 1.0f;
            sst.alpha = 1.0f;
            UiWidgetRect srect = {UI_A_LEFT | UI_A_TOP, 100, 100, 200, 200};
            int sidA = widget_add(&srn, UI_W_PANEL, "RA", 0, &srect, &sst);
            srect.x = 150;
            srect.y = 150;
            sst.bg[0] = 0.0f;
            sst.bg[2] = 1.0f;
            int sidB = widget_add(&srn, UI_W_PANEL, "RB", 0, &srect, &sst);
            // Прозрачный не попадает в кадр: 2 непрозрачных -> 2, один alpha=0 -> 1.
            RcVecShape sbuf[UI_MAX_PRIMS];
            if (ui_widget_build(&srn, g_WW, g_WH, sbuf, UI_MAX_PRIMS) != 2) {
                printf("SELF FAIL: rtb-n\n");
                bad = 1;
            }
            widget_get(&srn, sidA)->style.alpha = 0.0f;
            if (ui_widget_build(&srn, g_WW, g_WH, sbuf, UI_MAX_PRIMS) != 1) {
                printf("SELF FAIL: rtb-alpha\n");
                bad = 1;
            }
            widget_get(&srn, sidA)->style.alpha = 1.0f;
            // Перекрытие 150..300: выше по (z,id) на старте — B (id больше).
            if (ui_widget_hit(&srn, g_WW, g_WH, 200, 200) != sidB) {
                printf("SELF FAIL: rtb-hit0\n");
                bad = 1;
            }
            if (!widget_layer(&srn, sidA, +1)) {
                printf("SELF FAIL: rtb-raise\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 200, 200) != sidA) {
                printf("SELF FAIL: rtb-hit-top\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 110, 110) != sidA) { // B не покрывает
                printf("SELF FAIL: rtb-hit-a\n");
                bad = 1;
            }
            if (ui_widget_hit(&srn, g_WW, g_WH, 10, 10)) { // мимо обеих
                printf("SELF FAIL: rtb-hit-miss\n");
                bad = 1;
            }
            // Редакторская обёртка = тот же рантайм (не расходимся).
            UiWidgetTree keepRT = g_wt;
            g_wt = srn;
            if (wt_hit(200, 200) != ui_widget_hit(&g_wt, g_WW, g_WH, 200, 200)) {
                printf("SELF FAIL: rtb-wrap\n");
                bad = 1;
            }
            g_wt = keepRT;
        }
        // §14 F3c(1b): SELF-HOST — правим СОБСТВЕННЫЙ UI редактора теми же
        // механизмами (выбор/драг/ручки/инспектор), документ не трогаем.
        {
            widget_tree_init(&g_wt); // документ пуст
            g_editSelf = 1;
            g_act = &g_edWt;
            g_wtSel = 0;
            g_palHot = -1;
            g_dragId = 0;
            g_dragHandle = 0;
            ed_chrome_init();
            ed_chrome_sync();
            float px, py, pw, ph;
            ed_rect(g_edPal, &px, &py, &pw, &ph);
            if (pw <= 0 || ph <= 0) {
                printf("SELF FAIL: self-geom %f %f\n", pw, ph);
                bad = 1;
            }
            // Клик по паддингу панели (вне строк палитры) -> выбран виджет ХРОМА.
            UiEvent we;
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_DOWN;
            we.x = px + 3;
            we.y = py + 3;
            if (!wt_event(&we) || g_wtSel != g_edPal) {
                printf("SELF FAIL: self-pick %d\n", g_wtSel);
                bad = 1;
            }
            // Драг -> панель хрома уехала; правка юзера стабильна (sync не вернёт).
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = px + 3 + 40;
            we.y = py + 3 + 25;
            wt_event(&we);
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_UP;
            wt_event(&we);
            float x2, y2, w2, h2;
            ed_rect(g_edPal, &x2, &y2, &w2, &h2);
            if (x2 != px + 40 || y2 != py + 25 || w2 != pw || h2 != ph) {
                printf("SELF FAIL: self-move %f %f\n", x2, y2);
                bad = 1;
            }
            ed_chrome_sync();
            ed_rect(g_edPal, &x2, &y2, &w2, &h2);
            if (x2 != px + 40 || y2 != py + 25) {
                printf("SELF FAIL: self-keep %f %f\n", x2, y2);
                bad = 1;
            }
            // Инспектор в self-режиме правит ХРОМ (round), а не документ.
            UiWidget *cw = widget_get(&g_edWt, g_edPal);
            float r0 = cw->style.rounding;
            g_insField = IF_ROUND;
            g_insDownX = 0;
            g_insDownVal = r0;
            memset(&we, 0, sizeof we);
            we.kind = UI_EV_MOVE;
            we.x = 40;
            wt_event(&we); // 40 * 0.25 = +10
            if (cw->style.rounding <= r0 || g_wt.count != 0) {
                printf("SELF FAIL: self-ins %f\n", (double)cw->style.rounding);
                bad = 1;
            }
            g_insField = -1;
            // Хром НЕ пишем в layout.json (это вид инструмента, не документ):
            // коммит в self-режиме не создаёт файл.
            remove(UI_LAYOUT_PATH);
            wt_persist();
            {
                FILE *pf = fopen(UI_LAYOUT_PATH, "rb");
                if (pf) {
                    printf("SELF FAIL: self-persist\n");
                    fclose(pf);
                    bad = 1;
                }
            }
            // Слои хрома: 3 панели-корня, Lay + меняет порядок (как у документа).
            int before[UI_MAX_WIDGETS];
            int nb = wt_sib_sorted(0, before);
            if (nb != 3) {
                printf("SELF FAIL: self-lay-n %d\n", nb);
                bad = 1;
            }
            // Возврат в обычный режим + дефолтный хром для остальных проверок.
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            ed_chrome_init();
            ed_chrome_sync();
        }
        // §14 F3c(1b): список виджетов — ОКНО СО СКРОЛЛОМ (жалоба: строки
        // вылезали за панель; листать было нельзя).
        {
            widget_tree_init(&g_wt);
            g_editSelf = 0;
            g_act = &g_wt;
            g_wtSel = 0;
            g_treeScroll = 0;
            g_palHot = -1;
            for (int i = 0; i < 12; i++) wt_spawn(UI_W_BUTTON);
            int last = g_wtSel;
            tree_build();
            int rows = tree_rows_fit();
            if (g_treeN != 12 || rows < 1) {
                printf("SELF FAIL: tree-scroll-n %d rows %d\n", g_treeN, rows);
                bad = 1;
            }
            // Выбран последний -> он автоматически в кадре.
            g_wtSel = last;
            tree_build();
            {
                int idx = -1;
                for (int i = 0; i < g_treeN; i++)
                    if (g_treeId[i] == g_wtSel) idx = i;
                if (idx < 0 || idx < g_treeScroll || idx >= g_treeScroll + rows) {
                    printf("SELF FAIL: tree-scroll-sel %d sc %d rows %d\n", idx,
                           g_treeScroll, rows);
                    bad = 1;
                }
            }
            // Строка ниже последней видимой — мимо (не «вылезает» за панель).
            {
                float x, y, w, h;
                tree_rect(rows, &x, &y, &w, &h); // виртуальная строка вне окна
                float px, py, pw, ph;
                ed_rect(g_edTree, &px, &py, &pw, &ph);
                if (y + h <= py + ph) { // она вообще внутри панели -> не наш случай
                } else if (tree_hit(x + w * 0.5f, y + h * 0.5f)) {
                    printf("SELF FAIL: tree-scroll-clip\n");
                    bad = 1;
                }
            }
            // Колесо над панелью — листает; над пустотой — нет.
            {
                float px, py, pw, ph;
                ed_rect(g_edTree, &px, &py, &pw, &ph);
                g_wtSel = 0; // без выбора нет автоскролла к нему
                g_treeScroll = 0;
                tree_build();
                UiEvent we;
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = -1.0f; // вниз
                we.x = px + pw * 0.5f;
                we.y = py + ph * 0.5f;
                int sc0 = g_treeScroll;
                if (!wt_event(&we) || g_treeScroll <= sc0) {
                    printf("SELF FAIL: tree-wheel %d -> %d\n", sc0, g_treeScroll);
                    bad = 1;
                }
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = 1.0f;
                we.x = px + pw * 0.5f;
                we.y = py + ph * 0.5f;
                wt_event(&we);
                if (g_treeScroll != 0) {
                    printf("SELF FAIL: tree-wheel-back %d\n", g_treeScroll);
                    bad = 1;
                }
                memset(&we, 0, sizeof we);
                we.kind = UI_EV_WHEEL;
                we.value = -1.0f;
                we.x = (float)(g_vpx + 5);
                we.y = (float)(g_vpy + g_vph - 60);
                if (wt_event(&we)) {
                    printf("SELF FAIL: tree-wheel-steal\n");
                    bad = 1;
                }
            }
            widget_tree_init(&g_wt);
            g_wtSel = 0;
            g_treeScroll = 0;
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
    ed_chrome_init();        // §14 F3c(1b): собственный UI редактора — тоже виджеты
    ui_panels_viewport(g_panels, g_WW, g_WH, &g_vpx, &g_vpy, &g_vpw, &g_vph);
    ed_chrome_sync();
    {
        const char *sf = getenv("UI_EDIT_SELF");
        if (sf && sf[0] == '1') {
            g_editSelf = 1;
            g_act = &g_edWt;
            printf("ui_edit: self=1 (env): правим ХРОМ редактора\n");
        }
    }
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
    glfwSetScrollCallback(win, on_scroll);
    glfwSetKeyCallback(win, on_key);
    // Синхронизация кадра с дисплеем (по умолчанию ВКЛ). Без неё на слабом iGPU
    // кадр может попасть в композитор «полусобранным» — это и выглядит как
    // шлейф/косынка при драге. Бенчи/диагностика снимают: UI_EDIT_VSYNC=0.
    {
        const char *vs = getenv("UI_EDIT_VSYNC");
        glfwSwapInterval(vs && vs[0] == '0' ? 0 : 1);
    }
    {
        const char *nf = getenv("UI_EDIT_FULL");
        if (nf && nf[0] == '1') {
            g_noDirty = 1;
            printf("ui_edit: full=1 (dirty-rect выключен — диагноз шлейфа при драге)\n");
        }
    }
    g_curArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_curResize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    glfwShowWindow(win);
    glfwMaximizeWindow(win);
    glfwPollEvents();
    drain_ev();
    if (wt_persist_ok()) widget_json_load(&g_wt, wt_path()); // персист §14
    edit_push(); // первая заливка
    // Демо-виджеты для скриншота палитры/холста (UI_EDIT_SEED=1).
    if (getenv("UI_EDIT_SEED")) {
        wt_spawn(UI_W_PANEL);
        wt_spawn(UI_W_BUTTON);
        wt_spawn(UI_W_LABEL);
        edit_push();
    }
    { // UI_EDIT_SPAWN=N: сразу N кнопок — проверить/показать скролл списка
        const char *sp = getenv("UI_EDIT_SPAWN");
        if (sp && sp[0] > '0') {
            int k = atoi(sp);
            for (int i = 0; i < k; i++) wt_spawn(UI_W_BUTTON);
            edit_push();
            printf("ui_edit: spawn %d (widgets=%d)\n", k, g_wt.count);
        }
    }
    // Бенч драга: без vsync-капа, синтетика в цикле (ниже).
    const char *bench = getenv("UI_EDIT_BENCH");
    if (bench && bench[0]) {
        g_bench = 1;
        g_forceFull = !strcmp(bench, "full");
        {
            const char *ff = getenv("UI_EDIT_BENCH_FULL"); // форс полного пути при той же траектории
            if (ff && ff[0] == '1') g_forceFull = 1;
        }
        g_benchNoEv = !strcmp(bench, "none") || !strcmp(bench, "spawn");
        g_benchSpawn = !strcmp(bench, "spawn");
        {
            const char *bn = getenv("UI_EDIT_BENCH_N"); // залп: сколько спавнов
            if (bn && bn[0] > '0') g_benchNSpawn = atoi(bn);
        }
        g_benchResize = !strcmp(bench, "resize");
        {
            const char *bc = getenv("UI_EDIT_BENCH_CORNER"); // тянуть угол (BR), не край
            if (bc && bc[0] == '1') g_benchCorner = 1;
        }
        const char *bk = getenv("UI_EDIT_BENCH_K");
        if (bk && bk[0] > '0') g_benchK = atoi(bk);
        {
            const char *bev = getenv("UI_EDIT_BENCH_EVENTS"); // событий/кадр (мышь)
            if (bev && bev[0] > '0') g_benchEvents = atoi(bev);
            if (g_benchEvents < 1) g_benchEvents = 1;
            const char *nc = getenv("UI_EDIT_BENCH_NOCOAL");
            if (nc && nc[0] == '1') g_benchNoCoal = 1;
        }
        glfwSwapInterval(0);
        if (!g_wt.count) wt_spawn(UI_W_PANEL);
        for (int i = 0; i < UI_MAX_WIDGETS; i++)
            if (g_wt.alive[i]) {
                g_benchId = i + 1;
                break;
            }
        const char *bid = getenv("UI_EDIT_BENCH_ID"); // какую цель тащить (1..)
        if (bid && bid[0]) {
            int want = atoi(bid), seen = 0;
            for (int i = 0; i < UI_MAX_WIDGETS; i++)
                if (g_wt.alive[i] && ++seen == want) {
                    g_benchId = i + 1;
                    break;
                }
        }
        g_wtSel = g_benchId;
        { // Диагностика: раздуть цель (UI_EDIT_BENCH_SIZE=925x530) и повесить детей
            const char *bs = getenv("UI_EDIT_BENCH_SIZE");
            if (bs && bs[0]) {
                int sw = 0, sh = 0;
                UiWidget *bw = widget_get(&g_wt, g_benchId);
                if (bw && sscanf(bs, "%dx%d", &sw, &sh) == 2 && sw > 0 && sh > 0) {
                    bw->rect.anchors |= UI_A_LEFT | UI_A_TOP;
                    bw->rect.w = (float)sw;
                    bw->rect.h = (float)sh;
                }
            }
            const char *bkd = getenv("UI_EDIT_BENCH_KIDS");
            if (bkd && bkd[0] == '1') {
                UiWidgetRect cr;
                UiWidgetStyle cs;
                memset(&cr, 0, sizeof cr);
                memset(&cs, 0, sizeof cs);
                cr.anchors = UI_A_LEFT | UI_A_TOP;
                cs.alpha = 1.0f;
                cs.fontSize = 14.0f;
                cr.x = 300;
                cr.y = 150;
                cr.w = 150;
                cr.h = 22; // label
                widget_add(&g_wt, UI_W_LABEL, "Label1", g_benchId, &cr, &cs);
                cs.bg[0] = 0.22f;
                cs.bg[1] = 0.34f;
                cs.bg[2] = 0.62f;
                cs.bg[3] = 1.0f;
                cs.rounding = 4.0f;
                cr.x = 380;
                cr.y = 200;
                cr.w = 300;
                cr.h = 270; // button
                widget_add(&g_wt, UI_W_BUTTON, "Button1", g_benchId, &cr, &cs);
            }
        }
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
    while (!glfwWindowShouldClose(win) && !g_quit) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        { // живой FPS (для жалобы «со временем деградирует»)
            double nowT = glfwGetTime();
            if (g_lastT > 0.0) {
                float dt = (float)(nowT - g_lastT);
                if (dt > 0.0f) {
                    float f = 1.0f / dt;
                    g_fpsEma = g_fpsEma > 0.0f ? g_fpsEma * 0.9f + f * 0.1f : f;
                }
            }
            g_lastT = nowT;
        }
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
            // Подписи виджетов (button/label) + заголовки панелей хрома.
            static const float tcol[4] = {0.85f, 0.87f, 0.92f, 1.0f};
            static const float hcol[4] = {0.60f, 0.65f, 0.74f, 1.0f};
            {
                // §14 F3c: хром редактора = виджеты: заголовки — их имена/роль.
                float px, py, pw, ph;
                ed_rect(g_edPal, &px, &py, &pw, &ph);
                ui_text_quad(text, px + PAL_PAD, py + 4, "PALETTE", hcol);
                ed_rect(g_edTree, &px, &py, &pw, &ph);
                ui_text_quad(text, px + PAL_PAD, py + 4,
                             g_editSelf ? "EDITOR UI (F7)" : "WIDGETS", hcol);
                ed_rect(g_edIns, &px, &py, &pw, &ph);
                ui_text_quad(text, px + PAL_PAD, py + 4, "INSPECTOR", hcol);
            }
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
            // F3: подписи дерева виджетов (только видимые строки).
            tree_build();
            {
                int rows = tree_rows_fit();
                for (int i = g_treeScroll; i < g_treeN && i < g_treeScroll + rows; i++) {
                    const UiWidget *wd = widget_cget(g_act, g_treeId[i]);
                    if (!wd) continue;
                    float tx, ty, tw, th;
                    tree_rect(i - g_treeScroll, &tx, &ty, &tw, &th);
                    ui_text_quad(text, tx + 16 + (float)(g_treeDepth[i] * 10), ty + 3,
                                 wd->name, tcol);
                }
            }
            // F3b: подписи инспектора (заголовок + поля).
            if (g_wtSel) {
                const UiWidget *sw = widget_cget(g_act, g_wtSel);
                if (sw) {
                    float hx, hy, hw, hh;
                    ins_rect(-1, &hx, &hy, &hw, &hh);
                    (void)hw;
                    (void)hh;
                    ui_text_quad(text, hx + 6, hy + 2, sw->name, tcol);
                    for (int i = 0; i < IF_N; i++) {
                        float x, y, ww, hh2;
                        ins_rect(i, &x, &y, &ww, &hh2);
                        ui_text_quad(text, x + 6, y + 3, INS_LABEL[i], tcol);
                        char vb[32];
                        vb[0] = 0;
                        if (i == IF_X) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.x);
                        else if (i == IF_Y) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.y);
                        else if (i == IF_W) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.w);
                        else if (i == IF_H) snprintf(vb, sizeof vb, "%.0f", (double)sw->rect.h);
                        else if (i == IF_ROUND)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.rounding);
                        else if (i == IF_ALPHA)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.alpha);
                        else if (i == IF_FONT)
                            snprintf(vb, sizeof vb, "%.0f", (double)sw->style.fontSize);
                        else if (i == IF_BG_R)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[0]);
                        else if (i == IF_BG_G)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[1]);
                        else if (i == IF_BG_B)
                            snprintf(vb, sizeof vb, "%.2f", (double)sw->style.bg[2]);
                        if (vb[0]) ui_text_quad(text, x + 70, y + 3, vb, tcol);
                    }
                }
            }
            ui_text_quad(text, 10.0f, (float)(g_WH - 48),
                         "frontedit: click palette=add | click widget=select | blue edges"
                         " resize | wheel over list=scroll | F7 editor UI | Tab collapse | Esc",
                         hintCol);
            { // HUD: FPS + счётчик полных перерисовок (диагноз «съеденного fps»)
                char hud[48];
                snprintf(hud, sizeof hud, "FPS %.0f  full %d", (double)g_fpsEma, g_fullN);
                ui_text_quad(text, (float)(g_WW - 150), 6.0f, hud, hintCol);
            }
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
        // Бенч ЗАЛПА СПАВНОВ (жалоба: «лаг при создании +10 кнопок сразу»):
        // N кликов по палитре в ОДНОМ кадре — худший случай. Меряем залп в мс и
        // fps ПОСЛЕ (steady state с N виджетами). UI_EDIT_PERSIST=1 — честно, с файлом.
        if (g_benchSpawn && frames == 10) {
            double t0 = glfwGetTime();
            for (int i = 0; i < g_benchNSpawn; i++) {
                float px, py, pw, ph;
                pal_rect(1, &px, &py, &pw, &ph); // кнопка «Button»
                UiEvent be;
                memset(&be, 0, sizeof be);
                be.kind = UI_EV_DOWN;
                be.x = px + 5;
                be.y = py + 5;
                double a0 = glfwGetTime();
                route_event(&be);
                double a1 = glfwGetTime();
                if (getenv("UI_EDIT_TRACE"))
                    printf("  spawn[%d] event=%.3f ms\n", i, (a1 - a0) * 1000.0);
            }
            double t1 = glfwGetTime();
            RcVecShape tmp[UI_MAX_PRIMS];
            printf("ui_edit: bench spawn n=%d dt=%.3f ms each=%.3f ms shapes=%d widgets=%d\n",
                   g_benchNSpawn, (t1 - t0) * 1000.0,
                   (t1 - t0) * 1000.0 / (double)g_benchNSpawn, build_shapes(tmp),
                   g_wt.count);
        }
        // Бенч драга: DOWN в кадре 10, далее MOVE; замер FPS, выход по концу.
        if (g_bench && frames == 10) g_benchT0 = glfwGetTime();
        if (g_bench && !g_benchNoEv && frames >= 10 && frames < 10 + g_benchK) {
            UiEvent be;
            memset(&be, 0, sizeof be);
            if (frames == 10) {
                const UiWidget *bw = widget_cget(&g_wt, g_benchId);
                float bx, by, bww, bhh;
                widget_rect_px(bw, g_WW, g_WH, &bx, &by, &bww, &bhh);
                if (g_benchResize) { // хватаем правую ручку (или угол BR)
                    g_benchX = bx + bww - 1;
                    g_benchY = g_benchCorner ? by + bhh - 1 : by + bhh / 2;
                } else {
                    const char *bg = getenv("UI_EDIT_BENCH_GRAB"); // bg = за фон (не за детей)
                    if (bg && bg[0] == 'b') {
                        g_benchX = bx + bww * 0.2f;
                        g_benchY = by + bhh * 0.8f;
                    } else {
                        g_benchX = bx + bww / 2;
                        g_benchY = by + bhh / 2;
                    }
                }
                be.kind = UI_EV_DOWN;
                be.x = g_benchX;
                be.y = g_benchY;
                route_event(&be);
            } else {
                // N событий за кадр (как реальная мышь: опрос 125-1000Гц).
                // Коалесинг схлопывает подряд MOVE -> 1 заливка/кадр.
                float bx1, by1;
                if (g_benchResize) { // растём вправо (и вниз, если угол)
                    bx1 = g_benchX + (float)(frames - 10) * 3;
                    by1 = g_benchCorner ? g_benchY + (float)(frames - 10) * 2 : g_benchY;
                } else {
                    // Быстрый свип через весь вьюпорт (стресс: большие дельты).
                    int sw = g_vpw > 120 ? g_vpw - 80 : 200;
                    int sh = g_vph > 120 ? g_vph - 80 : 150;
                    bx1 = g_benchX + (float)((frames - 10) * 11 % sw);
                    by1 = g_benchY + (float)((frames - 10) * 5 % sh);
                }
                for (int j = 0; j < g_benchEvents; j++) {
                    UiEvent me;
                    memset(&me, 0, sizeof me);
                    me.kind = UI_EV_MOVE;
                    // подшаги: последний совпадает с одиночным событием
                    me.x = bx1 - (float)(g_benchEvents - 1 - j) * 0.3f;
                    me.y = by1 - (float)(g_benchEvents - 1 - j) * 0.2f;
                    if (g_benchNoCoal)
                        route_event(&me); // старое поведение: заливка на КАЖДОЕ событие
                    else
                        push_ev(&me);
                }
                if (!g_benchNoCoal) drain_ev();
            }
        }
        if (g_bench && frames == 10 + g_benchK - 1) {
            double dt = glfwGetTime() - g_benchT0;
            printf("ui_edit: bench %s steps=%d dt=%.3f fps=%.1f\n",
                   g_forceFull     ? "full"
                   : g_benchSpawn  ? "spawn"
                   : g_benchResize ? "resize"
                   : g_benchNoEv   ? "none"
                                   : "bands",
                   g_benchK, dt, g_benchK / dt);
            const UiWidget *bw = widget_cget(&g_wt, g_benchId);
            if (bw) {
                float rx, ry, rw, rh;
                widget_rect_px(bw, g_WW, g_WH, &rx, &ry, &rw, &rh);
                printf("ui_edit: bench geom id=%d x=%.1f y=%.1f w=%.1f h=%.1f\n",
                       g_benchId, rx, ry, rw, rh);
            }
            g_quit = 1;
        }
        // Окно прогресса (деградация во времени): каждые 100 шагов.
        if (g_bench && !g_benchNoEv && frames >= 10 && frames % 100 == 9 && frames > 10) {
            double dt = glfwGetTime() - g_benchT0;
            int done = frames - 9;
            printf("ui_edit: bench win steps=%d fps=%.1f\n", done, done / dt);
        }
        eng_pass_end(&t);
        comp_present_gl(prog, vao, &t, g_WW, g_WH);
        glfwSwapBuffers(win);
        glfwPollEvents();
        drain_ev(); // применить ввод раз за кадр (коалесинг MOVE)
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
