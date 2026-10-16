// Фаза 1: шов EngUi живьём. Своё окно, mesh-куб (3D) + 3 прямоугольника (UI).
// Клик по прямоугольнику = смена цвета (hit-test/state через шов).
// Заливка в vec-ядро ТОЛЬКО по dirty (иначе CPU-растр каждый кадр — позор).
// Запуск из корня репо (шейдеры относительные, как view_test): ./build/ui_test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rhi_gl.h" // ПЕРВЫМ: GLFW_INCLUDE_NONE + glad (иначе gl.h-конфликт)
#include "render_api.h"
#include "ui/ui_reflect.h" // X-макросы: дескрипторы из одного списка
#include "ui/ui_text.h" // Фаза 2: атлас глифов + quad/flush
#include "ui/ui_panels.h" // Фаза 3: меню/тулбар/иерархия/статус
#include "ui/ui_gizmo.h" // Гизмо: стрелки RGB=XYZ поверх вьюпорта
#include "ui/ui_undo.h" // Фаза 5: стек команд (selftest гизмо-коммита)
#include "ui/ui_undo.h" // Фаза 5: стек команд
#include "ui/ui_theme.h" // Фаза 5: одна тема + hot-reload
#include "rtarget.h"
#include "comp_gl.h"
#include "comp.h"
#include "rhi_gl.h"
#include "ui/ui.h"

#define W 1280
#define H 720 // дефолт до первого замера (тесты/selftest считают на нём)

// Живой размер окна (никакого хардкода в расчётах): фреймбуферные пиксели.
// UI-координаты = пиксели фреймбуфера; мышь из окна масштабируем.
static int g_WW = W, g_WH = H;
static float g_mouseKx = 1.0f, g_mouseKy = 1.0f;
static int g_needResize = 0;

static EngUi *g_ui = 0;
static RenderCore *g_vec = 0;
static UiPanels *g_panels = 0;
static UiGizmo *g_gizmo = 0;
static ScWorld g_scene;
static RcView g_view; // камера кадра (гизмо проецирует тем же PV что ядра)
static double g_mx = 0, g_my = 0;
static int g_capture = 0; // захват мыши модальным виджетом (FReply::CaptureMouse)
static GLFWcursor *g_curArrow = 0, *g_curResize = 0; // курсоры хрома
// Dirty-rect (инвалидация по-UE): гизмо-драг перезаливает только bbox стрелок
// (+ прошлый bbox — стереть было), панели — целиком и редко.
static int g_forceFull = 1; // стартовый/ресайз: полный upload
static int g_lastGx = 0, g_lastGy = 0, g_lastGw = 0, g_lastGh = 0;
// Док-раскладка (как UE: панели отъедают место сплиттерами, вьюпорту остаток).
// 3D рисуется ТОЛЬКО в rect (цель вьюпорта), панели — вокруг, не поверх 3D.
static int g_vpx = 220, g_vpy = 58, g_vpw = 840, g_vph = 636;
static RcTarget g_vp; // цель вьюпорта (пересоздаётся одна — не ядро/окно/мир)
static int g_vpOk = 0;

// Тестовый куб: ЛОКАЛЬНЫЙ (центр 0, h=3). Мир = TRS сущности (см. ниже).
// Грани P + idx 0,1,2/0,2,3, нормали наружу (проверено скриптом).
static float g_cube[36 * 9];
static float g_base[36 * 9]; // локальная копия (источник для TRS)
static void cube_make(void) {
    static const float P[6][4][3] = {
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},
        {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
        {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
    };
    static const float NN[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
    };
    static const float C[6][3] = {
        {0.8f, 0.15f, 0.1f}, {1, 0.8f, 0.1f}, {0.15f, 0.7f, 0.15f},
        {0.3f, 0.3f, 0.32f}, {0.15f, 0.3f, 0.8f}, {0.15f, 0.7f, 0.7f},
    };
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    int n = 0;
    for (int f = 0; f < 6; f++)
        for (int k = 0; k < 6; k++) {
            int q = idx[k];
            g_base[n * 9 + 0] = P[f][q][0] * 3.0f;
            g_base[n * 9 + 1] = P[f][q][1] * 3.0f;
            g_base[n * 9 + 2] = P[f][q][2] * 3.0f;
            g_base[n * 9 + 3] = NN[f][0];
            g_base[n * 9 + 4] = NN[f][1];
            g_base[n * 9 + 5] = NN[f][2];
            g_base[n * 9 + 6] = C[f][0];
            g_base[n * 9 + 7] = C[f][1];
            g_base[n * 9 + 8] = C[f][2];
            n++;
        }
}

// TRS сущности -> суп (только тест; продукт — подача из ECS).
// R = Ry(yaw)*Rx(pitch)*Rz(roll). Нормали: rotate+normalize (при неравном
// scale точная формула — через inverse-transpose; ограничение зафиксировано).
static void soup_pose(const ScTransform *tr) {
    float cy = cosf(tr->rot.x), sy = sinf(tr->rot.x);
    float cx = cosf(tr->rot.y), sx = sinf(tr->rot.y);
    float cz = cosf(tr->rot.z), sz = sinf(tr->rot.z);
    for (int i = 0; i < 36; i++) {
        float x = g_base[i * 9 + 0] * tr->scale.x;
        float y = g_base[i * 9 + 1] * tr->scale.y;
        float z = g_base[i * 9 + 2] * tr->scale.z;
        // Rz
        float x1 = x * cz - y * sz, y1 = x * sz + y * cz, z1 = z;
        // Rx
        float y2 = y1 * cx - z1 * sx, z2 = y1 * sx + z1 * cx, x2 = x1;
        // Ry + translate
        // Цвета едут из базы как есть (TRS их не трогает).
        g_cube[i * 9 + 6] = g_base[i * 9 + 6];
        g_cube[i * 9 + 7] = g_base[i * 9 + 7];
        g_cube[i * 9 + 8] = g_base[i * 9 + 8];
        g_cube[i * 9 + 0] = x2 * cy + z2 * sy + tr->pos.x;
        g_cube[i * 9 + 1] = y2 + tr->pos.y;
        g_cube[i * 9 + 2] = -x2 * sy + z2 * cy + tr->pos.z;
        float nx = g_base[i * 9 + 3], ny = g_base[i * 9 + 4], nz = g_base[i * 9 + 5];
        float nx1 = nx * cz - ny * sz, ny1 = nx * sz + ny * cz, nz1 = nz;
        float ny2 = ny1 * cx - nz1 * sx, nz2 = ny1 * sx + nz1 * cx, nx2 = nx1;
        float nx3 = nx2 * cy + nz2 * sy, ny3 = ny2, nz3 = -nx2 * sy + nz2 * cy;
        float il = 1.0f / (sqrtf(nx3 * nx3 + ny3 * ny3 + nz3 * nz3) + 1e-9f);
        g_cube[i * 9 + 3] = nx3 * il;
        g_cube[i * 9 + 4] = ny3 * il;
        g_cube[i * 9 + 5] = nz3 * il;
    }
}

static void ui_push_dirty(void) {
    // UiPrim и RcVecShape совпадают полями 1:1 (kind/a,b,c,d/rgba) —
    // копируем явно (strict aliasing не испытываем).
    // Порядок как SViewport::OnPaint: панели, затем гизмо поверх.
    const UiFrame *fr = 0;
    RcVecShape shapes[UI_MAX_PRIMS];
    int n = 0;
    if (g_gizmo) { // гизмо следует за selection/tool панелей (живой размер)
        ui_gizmo_select(g_gizmo, ui_panels_selected(g_panels));
        ui_gizmo_tool(g_gizmo, ui_panels_tool(g_panels));
    }
    // Док-раскладка: вьюпорт = остаток после панелей. Смена layout —
    // пересоздать ТОЛЬКО цель (как UE UpdateViewportRHI, не ядро/окно).
    {
        int x, y, w, h;
        ui_panels_viewport(g_panels, g_WW, g_WH, &x, &y, &w, &h);
        if (w < 64) w = 64;
        if (h < 64) h = 64;
        if (!g_vpOk || x != g_vpx || y != g_vpy || w != g_vpw || h != g_vph) {
            if (eng_target_resize(&g_vp, w, h)) {
                g_vpx = x;
                g_vpy = y;
                g_vpw = w;
                g_vph = h;
                g_vpOk = 1;
                g_view.resW = w; // камера с аспектом вьюпорта, не окна
                g_view.resH = h;
            } else {
                memset(&g_vp, 0, sizeof g_vp);
                g_vpOk = 0;
            }
        }
    }
    if (g_gizmo) ui_gizmo_viewport(g_gizmo, g_vpx, g_vpy, g_vpw, g_vph);
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
    int ngAll = 0;
    if (g_gizmo && !ui_panels_edit(g_panels)) {
        UiPrim gp[96]; // кольца: 3x24 сегмента + запас
        int ng = ui_gizmo_build(g_gizmo, &g_scene, &g_view, g_WW, g_WH, gp, 96);
        ngAll = ng;
        for (int i = 0; i < ng && n < UI_MAX_PRIMS; i++, n++) {
            shapes[n].kind = gp[i].kind;
            shapes[n].a = gp[i].a;
            shapes[n].b = gp[i].b;
            shapes[n].c = gp[i].c;
            shapes[n].d = gp[i].d;
            shapes[n].e = gp[i].e;
            shapes[n].f = gp[i].f;
            memcpy(shapes[n].rgba, gp[i].rgba, sizeof shapes[n].rgba);
        }
    }
    if (!n) return;
    // Dirty-rect (инвалидация по-UE): панели редко и целиком, гизмо-драг —
    // только bbox стрелок (прошлый+нынешний — стереть было). Флаг force —
    // старт/ресайз (fb только что пересоздан/пуст).
    {
        int gx0 = 1 << 30, gy0 = 1 << 30, gx1 = -1, gy1 = -1;
        for (int i = n - ngAll; i < n; i++) {
            float x0, y0, x1, y1;
            if (shapes[i].kind == 0) {
                x0 = shapes[i].a;
                y0 = shapes[i].b;
                x1 = shapes[i].a + shapes[i].c;
                y1 = shapes[i].b + shapes[i].d;
            } else if (shapes[i].kind == 1) {
                x0 = shapes[i].a - shapes[i].c;
                y0 = shapes[i].b - shapes[i].c;
                x1 = shapes[i].a + shapes[i].c;
                y1 = shapes[i].b + shapes[i].c;
            } else {
                x0 = x1 = shapes[i].a;
                y0 = y1 = shapes[i].b;
                float px[4] = {shapes[i].a, shapes[i].c, shapes[i].e, shapes[i].a};
                float py[4] = {shapes[i].b, shapes[i].d, shapes[i].f, shapes[i].b};
                for (int k = 0; k < (shapes[i].kind == 3 ? 3 : 2); k++) {
                    if (px[k] < x0) x0 = px[k];
                    if (px[k] > x1) x1 = px[k];
                    if (py[k] < y0) y0 = py[k];
                    if (py[k] > y1) y1 = py[k];
                }
            }
            if (x0 < gx0) gx0 = (int)floorf(x0) - 2;
            if (y0 < gy0) gy0 = (int)floorf(y0) - 2;
            if (x1 > gx1) gx1 = (int)ceilf(x1) + 2;
            if (y1 > gy1) gy1 = (int)ceilf(y1) + 2;
        }
        int panelsDirtyNow = panelsDirty;
        if (g_forceFull || panelsDirtyNow) {
            if (g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
            g_forceFull = 0;
            g_lastGx = (ngAll > 0) ? gx0 : 0;
            g_lastGy = (ngAll > 0) ? gy0 : 0;
            g_lastGw = (ngAll > 0) ? gx1 - gx0 : 0;
            g_lastGh = (ngAll > 0) ? gy1 - gy0 : 0;
        } else if (ngAll > 0) {
            int rx0 = gx0 < g_lastGx ? gx0 : g_lastGx;
            int ry0 = gy0 < g_lastGy ? gy0 : g_lastGy;
            int rx1 = gx1 > g_lastGx + g_lastGw ? gx1 : g_lastGx + g_lastGw;
            int ry1 = gy1 > g_lastGy + g_lastGh ? gy1 : g_lastGy + g_lastGh;
            if (g_vec->upload_vector_rect)
                g_vec->upload_vector_rect(g_vec, shapes, n, rx0, ry0, rx1 - rx0,
                                          ry1 - ry0);
            else if (g_vec->upload_vector)
                g_vec->upload_vector(g_vec, shapes, n);
            g_lastGx = gx0;
            g_lastGy = gy0;
            g_lastGw = gx1 - gx0;
            g_lastGh = gy1 - gy0;
        } else if (g_lastGw > 0) {
            // Гизмо ушло (tool select): стереть былое частичной заливкой.
            if (g_vec->upload_vector_rect)
                g_vec->upload_vector_rect(g_vec, shapes, n, g_lastGx, g_lastGy, g_lastGw,
                                          g_lastGh);
            else if (g_vec->upload_vector)
                g_vec->upload_vector(g_vec, shapes, n);
            g_lastGw = g_lastGh = 0;
        } else {
            if (g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
        }
    }
}

static int g_trace = 0; // UI_TRACE=1: лог ручных действий (только тест)
static int g_traceMoves = 0;
static double g_traceT = 0;

static void trace_commit(void) {
    // Что изменилось коммитом: selected + его transform (итог drag/скраба).
    unsigned sel = ui_panels_selected(g_panels);
    if (!sel || !sc_alive(&g_scene, sel)) return;
    const char *nm = sc_name(&g_scene, sel);
    ScTransform t = sc_get(&g_scene, sel);
    printf("[trace] commit %s pos=(%.2f,%.2f,%.2f) rot=(%.2f,%.2f,%.2f)\n",
           nm && nm[0] ? nm : "?", t.pos.x, t.pos.y, t.pos.z, t.rot.x, t.rot.y, t.rot.z);
}

static void route_event(const UiEvent *e) {
    // Приоритет как UE/Roblox: гизмо > панели (ручка первее строки под ней).
    // Модальные игнорируют чужие события сами (возврат unhandled).
    const char *kind = e->kind == UI_EV_DOWN ? "DOWN" : e->kind == UI_EV_UP ? "UP"
                         : e->kind == UI_EV_MOVE                            ? "MOVE"
                         : e->kind == UI_EV_KEY                             ? "KEY"
                         : e->kind == UI_EV_WHEEL                           ? "WHEEL"
                                                                            : "?";
    int logMove = 0;
    if (g_trace && e->kind == UI_EV_DOWN) g_traceMoves = 0; // новый жест — сначала лог
    if (g_trace && e->kind == UI_EV_MOVE) {
        // MOVE без спама: первое + раз в 200мс (drag виден, лог читаем).
        double now = glfwGetTime();
        if (g_traceMoves == 0 || now - g_traceT >= 0.2) {
            g_traceT = now;
            logMove = 1;
        }
        g_traceMoves++;
    }
    UiReply r = ui_unhandled();
    // Ручной режим (§11): гизмо скрыто, все события — панелям.
    if (!ui_panels_edit(g_panels)) {
        r = ui_gizmo_event(g_gizmo, &g_scene, &g_view, g_WW, g_WH, ui_panels_undo(g_panels),
                           e);
        if (r.handled) {
            g_capture = r.capture;
            if (r.dirty) ui_push_dirty();
            if (g_trace && e->kind == UI_EV_KEY) {
                printf("[trace] KEY %d mods=%d -> gizmo\n", e->key, e->mods);
            } else if (g_trace && (e->kind != UI_EV_MOVE || logMove)) {
                printf("[trace] %s %.0f,%.0f -> gizmo%s%s\n", kind, e->x, e->y,
                       r.capture ? " capture" : "", r.dirty ? " dirty" : "");
            }
            if (g_trace && e->kind == UI_EV_UP) trace_commit();
            return;
        }
    }
    r = ui_panels_event(g_panels, &g_scene, e);
    if (r.handled) g_capture = r.capture;
    if (r.dirty) ui_push_dirty();
    if (g_trace && e->kind == UI_EV_KEY) {
        printf("[trace] KEY %d mods=%d -> %s\n", e->key, e->mods,
               r.handled ? "handled" : "miss");
    } else if (g_trace && (e->kind != UI_EV_MOVE || logMove)) {
        printf("[trace] %s %.0f,%.0f -> panels%s%s\n", kind, e->x, e->y,
               r.handled ? "" : " (miss)", r.dirty ? " dirty" : "");
    }
    if (g_trace && e->kind == UI_EV_UP) trace_commit();
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
    (void)win;
    g_mx = x * g_mouseKx;
    g_my = y * g_mouseKy;
    // Курсор-резайз над хромом (рука чувствует край до клика).
    {
        static int lastHov = -1;
        int hov = g_panels ? ui_panels_hover(g_panels, (float)g_mx, (float)g_my) : 0;
        if (hov != lastHov) {
            lastHov = hov;
            if (g_curArrow && g_curResize) glfwSetCursor(win, hov ? g_curResize : g_curArrow);
        }
    }
    // Модальный скраб/драг едет на MOVE (вне захвата — дешёвый no-op у обоих).
    // Координаты — те же что DOWN/UP (g_mx, масштаб фреймбуфера):
    // гизмо-геометрия живёт в пикселях цели, сырые x/y окна врали у бортиков.
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
    (void)mods;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_Q || key == GLFW_KEY_W || key == GLFW_KEY_E || key == GLFW_KEY_R ||
        key == GLFW_KEY_Z || key == GLFW_KEY_Y || key == GLFW_KEY_TAB || key == GLFW_KEY_L) {
        UiEvent e;
        memset(&e, 0, sizeof e);
        e.kind = UI_EV_KEY;
        e.key = key; // GLFW_KEY_Q.. совпадают с ASCII; TAB=258 (см. панели)
        e.mods = mods; // 1 shift 2 ctrl 4 alt (для Ctrl+Z/Y)
        route_event(&e);
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        // Ввод/шов без окна: клик внутри/снаружи, dirty, цвета коммита.
        int bad = 0;
        EngUi *ui = eng_ui_create();
        const UiFrame *fr = 0;
        if (!eng_ui_build(ui, W, H, &fr) || fr->n != 3) {
            printf("SELF FAIL: first build\n");
            bad = 1;
        }
        if (eng_ui_build(ui, W, H, &fr)) {
            printf("SELF FAIL: second build must be clean\n");
            bad = 1;
        }
        UiEvent e;
        memset(&e, 0, sizeof e);
        e.kind = UI_EV_DOWN;
        e.x = 10;
        e.y = 10; // мимо всех
        if (eng_ui_event(ui, &e).handled) {
            printf("SELF FAIL: outside click dirty\n");
            bad = 1;
        }
        e.x = 170;
        e.y = 235; // центр второго (110,190,220,90)
        if (!eng_ui_event(ui, &e).handled) {
            printf("SELF FAIL: inside click clean\n");
            bad = 1;
        }
        if (!eng_ui_build(ui, W, H, &fr)) {
            printf("SELF FAIL: no rebuild after click\n");
            bad = 1;
        }
        // Второй виджет стал оранжевым (1.0,0.65,0.10).
        if (fr->prims[1].rgba[0] < 0.99f || fr->prims[1].rgba[1] < 0.6f) {
            printf("SELF FAIL: no alt color %f %f\n", fr->prims[1].rgba[0], fr->prims[1].rgba[1]);
            bad = 1;
        }
        eng_ui_destroy(ui);
        // Панели без окна: клик по строке + клавиша W.
        {
            ScWorld sw;
            sc_init(&sw);
            Entity a = sc_spawn(&sw, "A");
            Entity b = sc_spawn(&sw, "B");
            (void)a;
            UiPanels *pn = ui_panels_create();
            const UiFrame *pf = 0;
            if (!ui_panels_build(pn, &sw, 1280, 720, &pf) || pf->n < 4) {
                printf("SELF FAIL: panels build\n");
                bad = 1;
            }
            UiEvent pe;
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_DOWN;
            pe.x = 30;
            pe.y = 64 + 24; // вторая строка = B (ряды по 24 с y=64)
            if (!ui_panels_event(pn, &sw, &pe).handled || ui_panels_selected(pn) != b) {
                printf("SELF FAIL: panels select\n");
                bad = 1;
            }
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_KEY;
            pe.key = 'W';
            if (!ui_panels_event(pn, &sw, &pe).handled || ui_panels_tool(pn) != 1) {
                printf("SELF FAIL: panels tool\n");
                bad = 1;
            }
            // Скраб: DOWN на поле pos.x + MOVE +40px + UP => +2.0 в сцену.
            // Сетка инспектора: x=W-220+4, строки с y=116 (заголовок+26px ряды).
            ui_panels_build(pn, &sw, 1280, 720, &pf);
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_DOWN;
            pe.x = 1280 - 220 + 4 + 10;
            pe.y = 116 + 12;
            ui_panels_event(pn, &sw, &pe);
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_MOVE;
            pe.x = 1280 - 220 + 4 + 50;
            pe.y = 116 + 12;
            ui_panels_event(pn, &sw, &pe);
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_UP;
            ui_panels_event(pn, &sw, &pe);
            {
                ScTransform tb = sc_get(&sw, b);
                if (tb.pos.x < 1.99f || tb.pos.x > 2.01f) {
                    printf("SELF FAIL: scrub pos.x=%f\n", tb.pos.x);
                    bad = 1;
                }
            }
            if (!ui_panels_posed(pn)) {
                printf("SELF FAIL: posed flag\n");
                bad = 1;
            }
            // TRS-перекачка: pos.x=+10 двигает первый вершинный x ровно на 10.            cube_make();
            {
                ScTransform tp;
                memset(&tp, 0, sizeof tp);
                tp.pos.x = 10.0f;
                tp.scale.x = tp.scale.y = tp.scale.z = 1.0f;
                soup_pose(&tp);
                if (g_cube[0] != g_base[0] + 10.0f || g_cube[1] != g_base[1] ||
                    g_cube[2] != g_base[2]) {
                    printf("SELF FAIL: soup pose %f %f %f\n", g_cube[0], g_cube[1], g_cube[2]);
                    bad = 1;
                }
            }
            // Док-раскладка: вьюпорт = остаток (220,58,840,636); коллапс/Tab.
            {
                int x, y, w, h;
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 220 || y != 58 || w != 840 || h != 636) {
                    printf("SELF FAIL: viewport %d %d %d %d\n", x, y, w, h);
                    bad = 1;
                }
                // Сплиттер: DOWN на краю иерархии + MOVE +40 => hierW растёт.
                // (До коллапса: панель открыта.)
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 217;
                pe.y = 300; // край hierW=220 (строки до x=212 — хром)
                UiReply spr = ui_panels_event(pn, &sw, &pe);
                if (!spr.handled || !spr.capture) {
                    printf("SELF FAIL: splitter grab\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_MOVE;
                pe.x = 257;
                pe.y = 300;
                ui_panels_event(pn, &sw, &pe);
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_UP;
                ui_panels_event(pn, &sw, &pe);
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 260 || w != 800) {
                    printf("SELF FAIL: splitter viewport %d %d %d %d\n", x, y, w, h);
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 246;
                pe.y = 70; // кнопка коллапса иерархии (hierW=260 после сплиттера)
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: collapse miss\n");
                    bad = 1;
                }
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 0 || w != 1060) {
                    printf("SELF FAIL: collapsed viewport %d %d %d %d\n", x, y, w, h);
                    bad = 1;
                }
                // Tab: всё спрятать (фулскрин) и вернуть.
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 258; // TAB (не ASCII!)
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: tab miss\n");
                    bad = 1;
                }
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 0 || w != 1280 || h != 636) {
                    printf("SELF FAIL: tab viewport %d %d %d %d\n", x, y, w, h);
                    bad = 1;
                }
                // Ручной режим (§11): L вкл — строки инертны, сплиттер жив.
                // (Tab до этого всё спрятал — вернуть панели сначала.)
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 258;
                ui_panels_event(pn, &sw, &pe);
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 'L';
                if (!ui_panels_event(pn, &sw, &pe).handled || !ui_panels_edit(pn)) {
                    printf("SELF FAIL: edit on\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 30;
                pe.y = 64; // шапка в EDIT: начало drag панели (строки инертны)
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: edit header dead\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_UP;
                ui_panels_event(pn, &sw, &pe);
                if (ui_panels_selected(pn) != b) {
                    printf("SELF FAIL: edit row live\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 257;
                pe.y = 300; // край hierW=260 — сплиттер хватает и в EDIT
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: edit splitter dead\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_UP;
                ui_panels_event(pn, &sw, &pe);
                // Ховер + широкий захват (Фиттс): 249 = мимо в норме, край в EDIT.
                if (!ui_panels_hover(pn, 249, 300) || ui_panels_hover(pn, 100, 300)) {
                    printf("SELF FAIL: edit hover\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 'L';
                if (!ui_panels_event(pn, &sw, &pe).handled || ui_panels_edit(pn)) {
                    printf("SELF FAIL: edit off\n");
                    bad = 1;
                }
                // В норме 249 — мимо (захват 6px): широкий граб только в EDIT.
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 249;
                pe.y = 300;
                if (ui_panels_event(pn, &sw, &pe).handled || ui_panels_hover(pn, 249, 300)) {
                    printf("SELF FAIL: normal grab wide\n");
                    bad = 1;
                }
                // §12: style-скраб в EDIT (round +80px => +4.0) пишет theme.json.
                {
                    FILE *bf = fopen("engine/assets/theme.json", "rb");
                    long bsz = 0;
                    char *btmp = 0;
                    if (bf) {
                        fseek(bf, 0, SEEK_END);
                        bsz = ftell(bf);
                        fseek(bf, 0, SEEK_SET);
                        if (bsz > 0 && bsz < 65536) {
                            btmp = (char *)malloc((size_t)bsz);
                            if (btmp && fread(btmp, 1, (size_t)bsz, bf) != (size_t)bsz) {
                                free(btmp);
                                btmp = 0;
                            }
                        }
                        fclose(bf);
                    }
                    memset(&pe, 0, sizeof pe);
                    pe.kind = UI_EV_KEY;
                    pe.key = 'L';
                    ui_panels_event(pn, &sw, &pe); // EDIT вкл
                    ui_panels_build(pn, &sw, 1280, 720, &pf); // srows посчитать
                    memset(&pe, 0, sizeof pe);
                    pe.kind = UI_EV_DOWN;
                    pe.x = 1074;
                    pe.y = 128; // style-строка 0 (round)
                    ui_panels_event(pn, &sw, &pe);
                    memset(&pe, 0, sizeof pe);
                    pe.kind = UI_EV_MOVE;
                    pe.x = 1154;
                    pe.y = 128;
                    ui_panels_event(pn, &sw, &pe);
                    memset(&pe, 0, sizeof pe);
                    pe.kind = UI_EV_UP;
                    ui_panels_event(pn, &sw, &pe);
                    int roundOk = 0;
                    FILE *rf = fopen("engine/assets/theme.json", "rb");
                    if (rf) {
                        char rb[2048];
                        size_t rn = fread(rb, 1, sizeof rb - 1, rf);
                        rb[rn] = 0;
                        fclose(rf);
                        const char *qp = strstr(rb, "\"round\"");
                        float rv = -1;
                        if (qp && sscanf(strchr(qp, '['), "[%f", &rv) == 1 && rv > 3.9f &&
                            rv < 4.1f)
                            roundOk = 1;
                    }
                    if (!roundOk) {
                        printf("SELF FAIL: style scrub save\n");
                        bad = 1;
                    }
                    if (btmp) { // вернуть файл как был
                        FILE *wf = fopen("engine/assets/theme.json", "wb");
                        if (wf) {
                            fwrite(btmp, 1, (size_t)bsz, wf);
                            fclose(wf);
                        }
                        free(btmp);
                    }
                    memset(&pe, 0, sizeof pe);
                    pe.kind = UI_EV_KEY;
                    pe.key = 'L';
                    ui_panels_event(pn, &sw, &pe); // EDIT выкл
                }
                // §11 хвост: шапку налево->направо = swap; Ctrl+Z откатывает.
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 'L';
                ui_panels_event(pn, &sw, &pe); // EDIT вкл
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_DOWN;
                pe.x = 30;
                pe.y = 70; // левая шапка (hierW=260)
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: header grab\n");
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_MOVE;
                pe.x = 1200;
                pe.y = 70;
                ui_panels_event(pn, &sw, &pe);
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_UP;
                ui_panels_event(pn, &sw, &pe);
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 220) { // слева теперь инспектор (220), не hier (260)
                    printf("SELF FAIL: swap viewport x=%d\n", x);
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 'Z';
                pe.mods = 2; // Ctrl+Z в EDIT = откат раскладки
                if (!ui_panels_event(pn, &sw, &pe).handled) {
                    printf("SELF FAIL: layout undo\n");
                    bad = 1;
                }
                ui_panels_viewport(pn, 1280, 720, &x, &y, &w, &h);
                if (x != 260) {
                    printf("SELF FAIL: unswap viewport x=%d\n", x);
                    bad = 1;
                }
                memset(&pe, 0, sizeof pe);
                pe.kind = UI_EV_KEY;
                pe.key = 'L';
                ui_panels_event(pn, &sw, &pe); // EDIT выкл
                remove("engine/assets/layout.json"); // тест следов не оставляет
            }
            ui_panels_destroy(pn);
        }
        // Undo: push/undo/redo чистой логики + тема-фолбэк без файла.
        {
            UiUndo u;
            ui_undo_reset(&u);
            UiCmd c;
            if (ui_undo_undo(&u, &c)) {
                printf("SELF FAIL: undo empty\n");
                bad = 1;
            }
            if (!ui_undo_push(&u, 5, 2, 1.0f, 2.0f)) {
                printf("SELF FAIL: push\n");
                bad = 1;
            }
            if (ui_undo_push(&u, 5, 2, 1.0f, 1.0f)) {
                printf("SELF FAIL: push same must ignore\n");
                bad = 1;
            }
            if (!ui_undo_undo(&u, &c) || c.before != 1.0f || c.after != 2.0f) {
                printf("SELF FAIL: undo value\n");
                bad = 1;
            }
            if (!ui_undo_redo(&u, &c) || c.after != 2.0f) {
                printf("SELF FAIL: redo value\n");
                bad = 1;
            }
            if (!ui_undo_undo(&u, &c)) {
                printf("SELF FAIL: undo2\n");
                bad = 1;
            }
            if (!ui_undo_push(&u, 5, 3, 0.0f, 9.0f) || ui_undo_redo(&u, &c)) {
                printf("SELF FAIL: push must kill redo\n");
                bad = 1;
            }
            UiTheme th;
            ui_theme_dark(&th);
            if (ui_theme_load(&th, "/nonexistent/theme.json")) {
                printf("SELF FAIL: load missing\n");
                bad = 1;
            }
            if (th.bg[0] < 0.15f) {
                printf("SELF FAIL: dark default\n");
                bad = 1;
            }
            // §12: save/load roundtrip + style-скраб пишет файл.
            {
                const char *tp = "/tmp/opencode/selftest_theme.json";
                th.round = 8.0f;
                th.pad = 3.0f;
                th.bg[3] = 0.5f;
                if (!ui_theme_save(&th, tp)) {
                    printf("SELF FAIL: theme save\n");
                    bad = 1;
                }
                UiTheme th2;
                ui_theme_dark(&th2);
                if (!ui_theme_load(&th2, tp) || th2.round != 8.0f || th2.pad != 3.0f ||
                    th2.bg[3] != 0.5f) {
                    printf("SELF FAIL: theme roundtrip %f %f %f\n", th2.round, th2.pad,
                           th2.bg[3]);
                    bad = 1;
                }
                remove(tp);
            }
        }
        // Гизмо без окна: проекция + хит середины X-стрелки + drag + undo.
        {
            ScWorld gw;
            sc_init(&gw);
            Entity ge = sc_spawn(&gw, "G");
            ScTransform gt;
            memset(&gt, 0, sizeof gt);
            gt.pos.x = -24.0f;
            gt.pos.y = 56.0f;
            gt.pos.z = -16.0f;
            gt.scale.x = gt.scale.y = gt.scale.z = 1.0f;
            sc_set(&gw, ge, &gt);
            RcView gv;
            memset(&gv, 0, sizeof gv);
            {
                double a = -2.2;
                gv.camPos.x = -24.0f + 30.0f * (float)cos(a);
                gv.camPos.y = 60.0f;
                gv.camPos.z = -16.0f + 30.0f * (float)sin(a);
                float dx = -24.0f - gv.camPos.x, dy = 54.0f - gv.camPos.y,
                      dz = -16.0f - gv.camPos.z;
                float dl = sqrtf(dx * dx + dy * dy + dz * dz);
                gv.yaw = (float)atan2(dz, dx);
                gv.pitch = (float)asin(dy / dl);
            }
            gv.fov = 1.6f;
            gv.resW = 1280;
            gv.resH = 720;
            UiGizmo *gz = ui_gizmo_create();
            UiUndo gu;
            ui_undo_reset(&gu);
            ui_gizmo_tool(gz, 1);
            ui_gizmo_select(gz, ge);
            float ox, oy, tx, ty;
            float oo[3] = {-24.0f, 56.0f, -16.0f};
            float tt[3] = {-16.0f, 56.0f, -16.0f};
            if (!ui_gizmo_project(&gv, 1280, 720, oo, &ox, &oy) ||
                !ui_gizmo_project(&gv, 1280, 720, tt, &tx, &ty)) {
                printf("SELF FAIL: gizmo project offscreen\n");
                bad = 1;
            }
            UiEvent ge2;
            memset(&ge2, 0, sizeof ge2);
            ge2.kind = UI_EV_DOWN;
            ge2.x = (ox + tx) * 0.5f;
            ge2.y = (oy + ty) * 0.5f;
            UiReply gr = ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
            if (!gr.handled || !gr.capture) {
                printf("SELF FAIL: gizmo pick miss\n");
                bad = 1;
            }
            // Тянуть вдоль оси: +60px по направлению стрелки.
            {
                float dx = tx - ox, dy = ty - oy;
                float dl = sqrtf(dx * dx + dy * dy);
                dx /= dl;
                dy /= dl;
                memset(&ge2, 0, sizeof ge2);
                ge2.kind = UI_EV_MOVE;
                ge2.x = (ox + tx) * 0.5f + dx * 60.0f;
                ge2.y = (oy + ty) * 0.5f + dy * 60.0f;
                gr = ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
                if (!gr.handled) {
                    printf("SELF FAIL: gizmo drag\n");
                    bad = 1;
                }
            }
            // Frozen frame: ещё 5 шагов по +20px — дельты равны, монотонно
            // (ловит feedback-разнос: раньше позиция осциллировала с ростом).
            {
                float dx = tx - ox, dy = ty - oy;
                float dl = sqrtf(dx * dx + dy * dy);
                dx /= dl;
                dy /= dl;
                float px = (ox + tx) * 0.5f + dx * 60.0f;
                float py = (oy + ty) * 0.5f + dy * 60.0f;
                float prev = sc_get(&gw, ge).pos.x;
                float d0 = 0;
                for (int si = 1; si <= 5; si++) {
                    memset(&ge2, 0, sizeof ge2);
                    ge2.kind = UI_EV_MOVE;
                    ge2.x = px + dx * 20.0f * si;
                    ge2.y = py + dy * 20.0f * si;
                    gr = ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
                    if (!gr.handled) {
                        printf("SELF FAIL: gizmo drag step %d\n", si);
                        bad = 1;
                        break;
                    }
                    float cur = sc_get(&gw, ge).pos.x;
                    float dd = cur - prev;
                    prev = cur;
                    if (si == 1) {
                        d0 = dd;
                        if (d0 <= 0) {
                            printf("SELF FAIL: gizmo drag dir %f\n", d0);
                            bad = 1;
                        }
                    } else if (fabsf(dd - d0) > 1e-3f) {
                        printf("SELF FAIL: gizmo drag not linear step %d (%f vs %f)\n",
                               si, dd, d0);
                        bad = 1;
                    }
                }
            }
            memset(&ge2, 0, sizeof ge2);
            ge2.kind = UI_EV_UP;
            gr = ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
            if (!gr.handled || gr.capture) {
                printf("SELF FAIL: gizmo drop\n");
                bad = 1;
            }
            {
                ScTransform chk = sc_get(&gw, ge);
                // 60px вдоль 8-мировой стрелки: ожидание грубовато — лишь знак и порядок.
                if (chk.pos.x <= -24.0f || chk.pos.x > -16.0f) {
                    printf("SELF FAIL: gizmo moved x=%f\n", chk.pos.x);
                    bad = 1;
                }
                UiCmd gc;
                if (!ui_undo_undo(&gu, &gc) || gc.before != -24.0f) {
                    printf("SELF FAIL: gizmo undo commit\n");
                    bad = 1;
                }
            }
            // Мимо стрелков: пустая точка (10,10) — unhandled.
            memset(&ge2, 0, sizeof ge2);
            ge2.kind = UI_EV_DOWN;
            ge2.x = 10;
            ge2.y = 10;
            if (ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2).handled) {
                printf("SELF FAIL: gizmo false hit\n");
                bad = 1;
            }
            // Кольца (tool=2): хит точки 90° + drag по касательной + undo.
            ui_gizmo_tool(gz, 2);
            {
                float hx, hy, ax, ay, bx, by;
                if (!ui_gizmo_handle(gz, &gw, &gv, 0, 1, 90.0f, &hx, &hy) ||
                    !ui_gizmo_handle(gz, &gw, &gv, 0, 1, 80.0f, &ax, &ay) ||
                    !ui_gizmo_handle(gz, &gw, &gv, 0, 1, 100.0f, &bx, &by)) {
                    printf("SELF FAIL: ring handle\n");
                    bad = 1;
                } else {
                    float dx = bx - ax, dy = by - ay;
                    float dl = sqrtf(dx * dx + dy * dy);
                    dx /= dl;
                    dy /= dl;
                    memset(&ge2, 0, sizeof ge2);
                    ge2.kind = UI_EV_DOWN;
                    ge2.x = hx;
                    ge2.y = hy;
                    UiReply gr2 =
                        ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
                    if (!gr2.handled || !gr2.capture) {
                        printf("SELF FAIL: ring pick\n");
                        bad = 1;
                    }
                    memset(&ge2, 0, sizeof ge2);
                    ge2.kind = UI_EV_MOVE;
                    ge2.x = hx + dx * 30.0f;
                    ge2.y = hy + dy * 30.0f;
                    if (!ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2).handled) {
                        printf("SELF FAIL: ring drag\n");
                        bad = 1;
                    }
                    memset(&ge2, 0, sizeof ge2);
                    ge2.kind = UI_EV_UP;
                    ui_gizmo_event(gz, &gw, &gv, 1280, 720, &gu, &ge2);
                    ScTransform chk = sc_get(&gw, ge);
                    if (chk.rot.x == 0.0f && chk.rot.y == 0.0f && chk.rot.z == 0.0f) {
                        printf("SELF FAIL: ring no rot\n");
                        bad = 1;
                    }
                    UiCmd gc2;
                    if (!ui_undo_undo(&gu, &gc2)) {
                        printf("SELF FAIL: ring undo empty\n");
                        bad = 1;
                    } else {
                        // Undo возвращает triple в исходный ноль (поле — какое взялось).
                        ScTransform back = sc_get(&gw, ge);
                        ui_field_set_f(&back, &transform_fields[gc2.field],
                                       gc2.before);
                        sc_set(&gw, ge, &back);
                        ScTransform fin = sc_get(&gw, ge);
                        if (fin.rot.x != 0.0f || fin.rot.y != 0.0f || fin.rot.z != 0.0f) {
                            printf("SELF FAIL: ring undo roundtrip\n");
                            bad = 1;
                        }
                    }
                    // Кольца строятся (72 сегмента макс, часть клиппится).
                    UiPrim gout2[96];
                    ui_gizmo_select(gz, ge);
                    int nr = ui_gizmo_build(gz, &gw, &gv, 1280, 720, gout2, 96);
                    if (nr <= 0) {
                        printf("SELF FAIL: ring build %d\n", nr);
                        bad = 1;
                    }
                }
            }
            // Клиппинг: rect в стороне от сущности — стрелков 0 (не лезут наружу).
            {
                UiPrim gout[16];
                int nfull = ui_gizmo_build(gz, &gw, &gv, 1280, 720, gout, 16);
                // Инвариант клиппинга: всё излучённое — внутри rect.
                for (int gi = 0; gi < nfull; gi++) {
                    if (gout[gi].a < 0 || gout[gi].a > 1280 || gout[gi].c < 0 ||
                        gout[gi].c > 1280 || gout[gi].b < 0 || gout[gi].b > 720 ||
                        gout[gi].d < 0 || gout[gi].d > 720) {
                        printf("SELF FAIL: gizmo seg %d outside\n", gi);
                        bad = 1;
                    }
                }
                if (nfull <= 0) {
                    printf("SELF FAIL: gizmo empty full\n");
                    bad = 1;
                }
                ui_gizmo_viewport(gz, 0, 0, 10, 10);
                {
                    // Инвариант клиппинга: всё излучённое — внутри rect.
                    UiPrim gout2[96];
                    int nclip = ui_gizmo_build(gz, &gw, &gv, 1280, 720, gout2, 96);
                    for (int gi = 0; gi < nclip; gi++) {
                        if (gout2[gi].a < 0 || gout2[gi].a > 10 || gout2[gi].c < 0 ||
                            gout2[gi].c > 10 || gout2[gi].b < 0 || gout2[gi].b > 10 ||
                            gout2[gi].d < 0 || gout2[gi].d > 10) {
                            printf("SELF FAIL: ring clip outside\n");
                            bad = 1;
                        }
                    }
                }
                ui_gizmo_viewport(gz, 0, 0, 0, 0);
            }
            ui_gizmo_destroy(gz);
        }
        // X-макросы: один список -> 9 дескрипторов, оффсеты подряд, roundtrip.
        if (TRANSFORM_NFIELDS != 9) {
            printf("SELF FAIL: nfields=%d\n", TRANSFORM_NFIELDS);
            bad = 1;
        }
        {
            ScTransform t;
            memset(&t, 0, sizeof t);
            for (int i = 0; i < TRANSFORM_NFIELDS; i++) {
                if (transform_fields[i].off != (size_t)(i * 4)) {
                    printf("SELF FAIL: off %d=%zu\n", i, transform_fields[i].off);
                    bad = 1;
                }
                ui_field_set_f(&t, &transform_fields[i], (float)(i + 1) * 1.5f);
            }
            // Через дескрипторы писалось в НАСТОЯЩУЮ структуру?
            if (t.pos.x != 1.5f || t.rot.y != 7.5f || t.scale.z != 13.5f) {
                printf("SELF FAIL: struct not written %f %f %f\n", t.pos.x, t.rot.y, t.scale.z);
                bad = 1;
            }
            for (int i = 0; i < TRANSFORM_NFIELDS; i++) {
                float want = (float)(i + 1) * 1.5f;
                if (ui_field_get_f(&t, &transform_fields[i]) != want) {
                    printf("SELF FAIL: roundtrip %d\n", i);
                    bad = 1;
                }
            }
        }
        printf(bad ? "SELF FAIL\n" : "ui selftest OK\n");
        return bad;
    }
    if (!glfwInit()) {
        printf("ui_test: glfw FAIL\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(W, H, "ui_test: seam", 0, 0);
    if (!win) {
        printf("ui_test: window FAIL\n");
        return 1;
    }
    RenderCore *mesh = rc_create("mesh");
    RenderCore *vec = rc_create("vec");
    if (!mesh || !vec || !mesh->init(mesh, win) || !vec->init(vec, win)) {
        printf("ui_test: cores FAIL\n");
        return 1;
    }
    g_vec = vec;
    // Демо-сцена для иерархии (та же тройка что в EngView).
    // Hero несёт куб: демо-поза как в камере (иначе куб в начале координат).
    sc_init(&g_scene);
    {
        Entity scene = sc_spawn(&g_scene, "Scene");
        Entity hero = sc_spawn(&g_scene, "Hero");
        Entity sword = sc_spawn(&g_scene, "Sword");
        if (sword && scene) sc_parent(&g_scene, sword, scene);
        if (hero) {
            ScTransform t;
            memset(&t, 0, sizeof t);
            t.pos.x = -24.0f;
            t.pos.y = 56.0f;
            t.pos.z = -16.0f;
            t.scale.x = t.scale.y = t.scale.z = 1.0f;
            sc_set(&g_scene, hero, &t);
        }
        (void)hero;
    }
    cube_make();
    // Куб следует за Hero (инспектор двигает сущность — суп перекачивается).
    {
        Entity hero = 0, list[64];
        int n = sc_list(&g_scene, list, 64);
        for (int i = 0; i < n; i++) {
            const char *nm = sc_name(&g_scene, list[i]);
            if (nm && !strcmp(nm, "Hero")) hero = list[i];
        }
        if (hero) {
            ScTransform t = sc_get(&g_scene, hero);
            soup_pose(&t);
            printf("ui_test: hero=%u pos=(%.1f,%.1f,%.1f) v0=(%.1f,%.1f,%.1f)\n", hero, t.pos.x,
                   t.pos.y, t.pos.z, g_cube[0], g_cube[1], g_cube[2]);
        } else printf("ui_test: HERO NOT FOUND\n");
    }
    RcMesh m;
    m.verts = g_cube;
    m.nverts = 36;
    if (mesh->upload_mesh) mesh->upload_mesh(mesh, &m);
    // Общая цель создаётся в ensure ниже (под живой размер, не 1280x720).
    // (Декларация RcTarget t — там же, перед циклом; дублей не плодим.)
    unsigned prog = rhi_gl_prog("engine/shaders/blit/blit.vert", "engine/shaders/blit/blit.frag");
    unsigned vao = rhi_gl_empty_vao();
    if (!prog || !vao) {
        printf("ui_test: blit FAIL\n");
        return 1;
    }
    g_ui = eng_ui_create();
    if (!g_ui) {
        printf("ui_test: ui FAIL\n");
        return 1;
    }
    g_panels = ui_panels_create();
    if (!g_panels) {
        printf("ui_test: panels FAIL\n");
        return 1;
    }
    UiText *text = ui_text_init("engine/assets/mono.ttf", 16.0f);
    if (!text) printf("ui_test: text OFF (rects only)\n");
    ui_push_dirty(); // первая заливка (dirty=1 из create): строит и строки
    // Дефолтный select: Hero (ПОСЛЕ первой сборки — строкам есть куда попасть).
    {
        UiEvent se;
        memset(&se, 0, sizeof se);
        se.kind = UI_EV_DOWN;
        se.x = 30;
        se.y = 100; // строка Hero (64 Scene, 88 Hero, 112 Sword)
        if (ui_panels_event(g_panels, &g_scene, &se).dirty) ui_push_dirty();
    }
    // Дефолтный tool: move (стрелки видны сразу; W/E/R переключают).
    // UI_TOOL=2 для пруфа колец без мыши.
    // FUI_DEMO=drag: скриптованный drag X-стрелки (+100px) для пруфа шлейфа.
    {
        UiEvent ke;
        memset(&ke, 0, sizeof ke);
        ke.kind = UI_EV_KEY;
        ke.key = 'W';
        if (ui_panels_event(g_panels, &g_scene, &ke).dirty) ui_push_dirty();
        const char *te = getenv("UI_TOOL");
        if (te && te[0] == '2') {
            ke.key = 'E';
            if (ui_panels_event(g_panels, &g_scene, &ke).dirty) ui_push_dirty();
        }
    }
    glfwSetMouseButtonCallback(win, on_mouse);
    glfwSetCursorPosCallback(win, on_move);
    glfwSetKeyCallback(win, on_key);
    g_curArrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    g_curResize = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    // Полноразмерное окно (как UE: вьюпорт-герой, не окошко 1280x720).
    // Игнор WM = остаёмся 1280x720 (тесты детерминированы).
    glfwShowWindow(win);
    glfwMaximizeWindow(win);
    glfwPollEvents();
    // UI_TRACE=1: лог ручных действий (только ui_test, в библиотеке нет).
    {
        const char *tr = getenv("UI_TRACE");
        g_trace = (tr && tr[0] != '0') ? 1 : 0;
        if (g_trace) printf("[trace] on (DOWN/UP + MOVE/200ms + commit)\n");
    }
    // Замер без vsync (FUI_NOVSYNC=1): uncapped throughput + цена push.
    // FUI_PUSH=N: N лишних полных push+upload в кадр (модель drag-нагрузки).
    if (getenv("FUI_NOVSYNC") && getenv("FUI_NOVSYNC")[0] != '0') glfwSwapInterval(0);
    int fuiPush = 0;
    {
        const char *fp = getenv("FUI_PUSH");
        if (fp && fp[0]) fuiPush = atoi(fp);
    }
    // Замер реального фреймбуфера (окно могли развернуть/экраны HiDPI).
    {
        int fw = 0, fh = 0, ww = 0, wh = 0;
        glfwGetFramebufferSize(win, &fw, &fh);
        glfwGetWindowSize(win, &ww, &wh);
        if (fw > 0 && fh > 0) {
            g_WW = fw;
            g_WH = fh;
            if (ww > 0 && wh > 0) {
                g_mouseKx = (float)fw / (float)ww;
                g_mouseKy = (float)fh / (float)wh;
            }
        }
        printf("ui_test: window %dx%d K=%.3f/%.3f\n", g_WW, g_WH, g_mouseKx, g_mouseKy);
    }
    // Статичная камера на куб (как vw_view mesh, orbA=-2.2).
    RcView v;
    memset(&v, 0, sizeof v);
    {
        float lookX = -24.0f, lookY = 54.0f, lookZ = -16.0f;
        double a = -2.2;
        v.camPos.x = lookX + 30.0f * (float)cos(a);
        v.camPos.y = 60.0f;
        v.camPos.z = lookZ + 30.0f * (float)sin(a);
        float dx = lookX - v.camPos.x, dy = lookY - v.camPos.y, dz = lookZ - v.camPos.z;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
    }
    v.fov = 1.6f;
    v.resW = W;
    v.resH = H;
    v.sunDir.x = 0.4f;
    v.sunDir.y = 0.75f;
    v.sunDir.z = 0.3f;
    v.gamma = 0.611f;
    v.exposure = 0.508f;
    v.fog = 0.387f;
    v.shadowOn = 1.0f;
    v.maxSteps = 100.0f;
    g_view = v; // гизмо проецирует той же камерой
    // Цели под живой размер окна (создание тут, пересоздание в цикле).
    RcTarget t;
    memset(&t, 0, sizeof t);
    int g_mainW = 0, g_mainH = 0; // размер, под который созданы цели
    g_gizmo = ui_gizmo_create();
    if (!g_gizmo) {
        printf("ui_test: gizmo FAIL\n");
        return 1;
    }
    ui_gizmo_select(g_gizmo, ui_panels_selected(g_panels));
    ui_gizmo_tool(g_gizmo, ui_panels_tool(g_panels));
    ui_push_dirty(); // первая заливка СО стрелками (гизмо моложе первого push)
    // FUI_DEMO=drag: скриптованный drag X-стрелки (+100px) для пруфа шлейфа.
    // (Тут гизмо уже живёт и синхронизировано.)
    {
        const char *dd = getenv("FUI_DEMO");
        if (dd && (!strcmp(dd, "drag") || !strcmp(dd, "dragfar") || !strcmp(dd, "edit") ||
                   !strcmp(dd, "swap"))) {
            int steps = !strcmp(dd, "dragfar") ? 30 : 5; // far: +600px до бортика
            float hx, hy;
            if (ui_gizmo_handle(g_gizmo, &g_scene, &g_view, 0, 0, 50.0f, &hx, &hy)) {
                UiEvent de;
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_DOWN;
                de.x = hx;
                de.y = hy;
                route_event(&de);
                for (int di = 1; di <= steps; di++) {
                    memset(&de, 0, sizeof de);
                    de.kind = UI_EV_MOVE;
                    de.x = hx + di * 20.0f;
                    de.y = hy;
                    route_event(&de);
                    if (g_trace) {
                        unsigned se = ui_panels_selected(g_panels);
                        ScTransform st = sc_get(&g_scene, se);
                        printf("[trace] step %d mouse=%.0f pos=(%.2f,%.2f,%.2f)\n",
                               di, hx + di * 20.0f, st.pos.x, st.pos.y, st.pos.z);
                    }
                }
                memset(&de, 0, sizeof de);
                de.kind = UI_EV_UP;
                de.x = hx + 5 * 20.0f;
                de.y = hy;
                route_event(&de);
                printf("ui_test: demo drag done\n");
            } else {
                printf("ui_test: demo drag NO HANDLE\n");
            }
        }
    }
    printf("ui_test: RUN (click rects, Esc/close to quit)\n");
    // FUI_DEMO=swap: скрипт-пруф swap доков (§11 хвост): L + шапка направо.
    {
        const char *de3 = getenv("FUI_DEMO");
        if (de3 && !strcmp(de3, "swap")) {
            UiEvent se3;
            memset(&se3, 0, sizeof se3);
            se3.kind = UI_EV_KEY;
            se3.key = 'L';
            route_event(&se3);
            memset(&se3, 0, sizeof se3);
            se3.kind = UI_EV_DOWN;
            se3.x = 100;
            se3.y = 70;
            route_event(&se3);
            memset(&se3, 0, sizeof se3);
            se3.kind = UI_EV_MOVE;
            se3.x = (float)g_WW - 100;
            se3.y = 70;
            route_event(&se3);
            memset(&se3, 0, sizeof se3);
            se3.kind = UI_EV_UP;
            se3.x = (float)g_WW - 100;
            se3.y = 70;
            route_event(&se3);
            int sx, sy, sw2, sh2;
            ui_panels_viewport(g_panels, g_WW, g_WH, &sx, &sy, &sw2, &sh2);
            printf("ui_test: swap vp x=%d (want insp left)\n", sx);
        }
    }
    // FUI_DEMO=edit: скрипт-пруф ручного режима (§11): L + клик по строке.
    {
        const char *de2 = getenv("FUI_DEMO");
        if (de2 && !strcmp(de2, "edit")) {
            UiEvent ke2;
            memset(&ke2, 0, sizeof ke2);
            ke2.kind = UI_EV_KEY;
            ke2.key = 'L';
            route_event(&ke2);
            printf("ui_test: edit=%d (want 1)\n", ui_panels_edit(g_panels));
            UiEvent re;
            memset(&re, 0, sizeof re);
            re.kind = UI_EV_DOWN;
            re.x = 30;
            re.y = 100; // строка иерархии — в EDIT инертна
            route_event(&re);
            printf("ui_test: edit=%d sel-stays\n", ui_panels_edit(g_panels));
        }
    }
    // Доказательство картинкой (как VOX_SHOT): UI_SHOT=shot.ppm UI_SHOT_AT=60.
    const char *shot = getenv("UI_SHOT");
    int shotAt = 60;
    {
        const char *sa = getenv("UI_SHOT_AT");
        if (sa && sa[0]) shotAt = atoi(sa);
    }
    int frames = 0;
    static const float white[4] = {0.85f, 0.85f, 0.87f, 1.0f};
    double tPrev = glfwGetTime();
    char fpsBuf[64] = "ui_test";
    // Замер стадий (FUI_TIME=1): mesh/vec/text/swap ms каждые 120 кадров.
    int wantTime = getenv("FUI_TIME") && getenv("FUI_TIME")[0] != '0';
    double tMesh = 0, tVec = 0, tText = 0, tSwap = 0;
    while (!glfwWindowShouldClose(win)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        // Живой размер: окно могли развернуть/перетянуть (цели — только они).
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
                g_mainW = g_mainH = 0; // ensure ниже пересоздаст
                if (g_vpOk) {
                    eng_target_free(&g_vp);
                    memset(&g_vp, 0, sizeof g_vp);
                    g_vpOk = 0;
                }
                // fb vec-ядра под новый размер (иначе оверлей стянется).
                RcView vu;
                memset(&vu, 0, sizeof vu);
                vu.resW = g_WW;
                vu.resH = g_WH;
                if (vec->frame) vec->frame(vec, &vu);
                ui_push_dirty(); // перерастрировать UI в новом размере
                printf("ui_test: resize %dx%d\n", g_WW, g_WH);
            }
        }
        // Общая цель под живой размер (создание/пересоздание — только цель).
        if (!eng_target_resize(&t, g_WW, g_WH)) {
            printf("ui_test: target FAIL\n");
            return 1;
        }
        g_mainW = g_WW;
        g_mainH = g_WH;
        // Инспектор/гизмо тронули трансформ — перекачать суп из ECS.
        if (ui_panels_posed(g_panels) || (g_gizmo && ui_gizmo_posed(g_gizmo))) {
            unsigned sel = ui_panels_selected(g_panels);
            if (sel && sc_alive(&g_scene, sel)) {
                ScTransform t = sc_get(&g_scene, sel);
                soup_pose(&t);
                RcMesh mm;
                mm.verts = g_cube;
                mm.nverts = 36;
                if (mesh->upload_mesh) mesh->upload_mesh(mesh, &mm);
            }
        }
        g_view.resW = g_vpOk ? g_vpw : W; // аспект вьюпорта (или окна при отказе)
        g_view.resH = g_vpOk ? g_vph : H;
        // Чистим общую цель САМИ, явно (W2: решение вызывающего, не ядра).
        // begin помечает passClear=0 (ядрам чистить нечего — уже чисто).
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glViewport(0, 0, g_WW, g_WH);
        glClearColor(0.05f, 0.07f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        eng_pass_begin(&t, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, 0);
        double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        if (wantTime) s0 = glfwGetTime();
        if (g_vpOk) {
            // 3D ТОЛЬКО во вьюпорт (своя цель, свой аспект)...
            eng_pass_begin(&g_vp, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, 1);
            if (mesh->frame_to) mesh->frame_to(mesh, &g_view, &g_vp);
            eng_pass_end(&g_vp);
            // ...затем GPU-блит в общую цель на место rect (0 копий CPU).
            // GL y-вверх: dst перевёрнут относительно UI y-вниз.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, g_vp.fbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, t.fbo);
            glBlitFramebuffer(0, 0, g_vpw, g_vph, g_vpx, g_WH - (g_vpy + g_vph),
                              g_vpx + g_vpw, g_WH - g_vpy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            if (mesh->frame_to) mesh->frame_to(mesh, &g_view, &t);
        }
        if (wantTime) s1 = glfwGetTime();
        if (vec->frame_to) vec->frame_to(vec, &v, &t);
        if (wantTime) s2 = glfwGetTime();
        if (text) {
            // Панели: текст (имена/режимы/статус). fps — оверлеем ВНУТРИ
            // вьюпорта (как UE viewport stats), не отдельной полосой.
            // В ЦЕЛЬ (fbo), до презента — иначе презент перекроет.
            double now = glfwGetTime();
            if (now - tPrev >= 0.5) {
                tPrev = now;
                snprintf(fpsBuf, sizeof fpsBuf, "ui_test %d", frames);
            }
            ui_panels_text(g_panels, &g_scene, text, g_WW, g_WH);
            ui_text_quad(text, (float)(g_vpx + 10), (float)(g_vpy + 8), fpsBuf, white);
            glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            glViewport(0, 0, g_WW, g_WH);
            ui_text_flush(text, g_WW, g_WH);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (wantTime) s3 = glfwGetTime();
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
                    printf("ui_test: shot %s\n", shot);
                }
                free(px);
            }
        }
        comp_present_gl(prog, vao, &t, g_WW, g_WH);
        glfwSwapBuffers(win);
        glfwPollEvents();
        if (wantTime) {
            s4 = glfwGetTime();
            tMesh += (s1 - s0) * 1000.0;
            tVec += (s2 - s1) * 1000.0;
            tText += (s3 - s2) * 1000.0;
            tSwap += (s4 - s3) * 1000.0;
        }
        if (fuiPush > 0) {
            // Модель drag-нагрузки: полные push+upload мимо dirty (как MOVE).
            const UiFrame *frDbg = 0;
            double p0 = glfwGetTime();
            for (int pi = 0; pi < fuiPush; pi++) {
                ui_panels_build(g_panels, &g_scene, g_WW, g_WH, &frDbg);
                ui_push_dirty();
            }
            double p1 = glfwGetTime();
            tVec += (p1 - p0) * 1000.0;
        }
        if (++frames == 120) printf("ui_test: 120 frames OK (seam alive)\n");
        if (wantTime && frames % 120 == 0 && frames > 0)
            printf("ui_test: ms mesh=%.1f vec=%.1f text=%.1f swap=%.1f (avg/frame)\n",
                   tMesh / frames, tVec / frames, tText / frames, tSwap / frames);
    }
    printf("ui_test: frames=%d DONE\n", frames);
    if (g_curArrow) glfwDestroyCursor(g_curArrow);
    if (g_curResize) glfwDestroyCursor(g_curResize);
    g_curArrow = g_curResize = 0;
    ui_gizmo_destroy(g_gizmo);
    ui_panels_destroy(g_panels);
    ui_text_destroy(text);
    eng_ui_destroy(g_ui);
    return 0;
}
