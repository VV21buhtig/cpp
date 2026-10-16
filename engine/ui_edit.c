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
#include "ui/ui_widget.h" // дерево виджетов конструктора (§14 F1: модель)
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
    if (n && g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, n);
}

static void route_event(const UiEvent *e) {
    UiReply r = ui_panels_event(g_panels, &g_scene, e);
    if (r.dirty) edit_push();
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
            ui_text_quad(text, 10.0f, (float)(g_WH - 48),
                         "frontedit: drag blue edges | scrub style rows | Tab collapse | Esc quit",
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
