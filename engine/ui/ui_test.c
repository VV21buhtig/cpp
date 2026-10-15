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
#include "rtarget.h"
#include "comp_gl.h"
#include "rhi_gl.h"
#include "ui/ui.h"

#define W 1280
#define H 720

static EngUi *g_ui = 0;
static RenderCore *g_vec = 0;
static UiPanels *g_panels = 0;
static ScWorld g_scene;
static double g_mx = 0, g_my = 0;

// Тестовый куб (только тест! продуктовая подача — vw_feed_mesh).
// Грани P + idx 0,1,2/0,2,3, нормали наружу (проверено скриптом).
static float g_cube[36 * 9];
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
            g_cube[n * 9 + 0] = -24.0f + P[f][q][0] * 3.0f;
            g_cube[n * 9 + 1] = 56.0f + P[f][q][1] * 3.0f;
            g_cube[n * 9 + 2] = -16.0f + P[f][q][2] * 3.0f;
            g_cube[n * 9 + 3] = NN[f][0];
            g_cube[n * 9 + 4] = NN[f][1];
            g_cube[n * 9 + 5] = NN[f][2];
            g_cube[n * 9 + 6] = C[f][0];
            g_cube[n * 9 + 7] = C[f][1];
            g_cube[n * 9 + 8] = C[f][2];
            n++;
        }
}

static void ui_push_dirty(void) {
    // UiPrim и RcVecShape совпадают полями 1:1 (kind/a,b,c,d/rgba) —
    // копируем явно (strict aliasing не испытываем).
    const UiFrame *fr = 0;
    if (!ui_panels_build(g_panels, &g_scene, W, H, &fr)) return; // чисто
    RcVecShape shapes[UI_MAX_PRIMS];
    for (int i = 0; i < fr->n; i++) {
        shapes[i].kind = fr->prims[i].kind;
        shapes[i].a = fr->prims[i].a;
        shapes[i].b = fr->prims[i].b;
        shapes[i].c = fr->prims[i].c;
        shapes[i].d = fr->prims[i].d;
        memcpy(shapes[i].rgba, fr->prims[i].rgba, sizeof shapes[i].rgba);
    }
    if (g_vec->upload_vector) g_vec->upload_vector(g_vec, shapes, fr->n);
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
        if (ui_panels_event(g_panels, &g_scene, &e)) ui_push_dirty();
    }
}

static void on_move(GLFWwindow *win, double x, double y) {
    (void)win;
    (void)x;
    (void)y;
    // Hover по панелям не нужен (клики + клавиши). Мышь держим для будущего.
    g_mx = x;
    g_my = y;
}

static void on_key(GLFWwindow *win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_Q || key == GLFW_KEY_W || key == GLFW_KEY_E || key == GLFW_KEY_R) {
        UiEvent e;
        memset(&e, 0, sizeof e);
        e.kind = UI_EV_KEY;
        e.key = key; // GLFW_KEY_Q/W/E/R = ASCII Q/W/E/R
        if (ui_panels_event(g_panels, &g_scene, &e)) ui_push_dirty();
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
        if (eng_ui_event(ui, &e)) {
            printf("SELF FAIL: outside click dirty\n");
            bad = 1;
        }
        e.x = 170;
        e.y = 235; // центр второго (110,190,220,90)
        if (!eng_ui_event(ui, &e)) {
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
            if (!ui_panels_event(pn, &sw, &pe) || ui_panels_selected(pn) != b) {
                printf("SELF FAIL: panels select\n");
                bad = 1;
            }
            memset(&pe, 0, sizeof pe);
            pe.kind = UI_EV_KEY;
            pe.key = 'W';
            if (!ui_panels_event(pn, &sw, &pe) || ui_panels_tool(pn) != 1) {
                printf("SELF FAIL: panels tool\n");
                bad = 1;
            }
            ui_panels_destroy(pn);
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
    cube_make();
    RcMesh m;
    m.verts = g_cube;
    m.nverts = 36;
    if (mesh->upload_mesh) mesh->upload_mesh(mesh, &m);
    RcTarget t;
    memset(&t, 0, sizeof t);
    if (!comp_target_gl_new(&t, W, H)) {
        printf("ui_test: target FAIL\n");
        return 1;
    }
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
    // Демо-сцена для иерархии (та же тройка что в EngView).
    sc_init(&g_scene);
    {
        Entity scene = sc_spawn(&g_scene, "Scene");
        Entity hero = sc_spawn(&g_scene, "Hero");
        Entity sword = sc_spawn(&g_scene, "Sword");
        if (sword && scene) sc_parent(&g_scene, sword, scene);
        (void)hero;
    }
    g_panels = ui_panels_create();
    if (!g_panels) {
        printf("ui_test: panels FAIL\n");
        return 1;
    }
    UiText *text = ui_text_init("engine/assets/mono.ttf", 16.0f);
    if (!text) printf("ui_test: text OFF (rects only)\n");
    ui_push_dirty(); // первая заливка (dirty=1 из create)
    glfwSetMouseButtonCallback(win, on_mouse);
    glfwSetCursorPosCallback(win, on_move);
    glfwSetKeyCallback(win, on_key);
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
    printf("ui_test: RUN (click rects, Esc/close to quit)\n");
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
    while (!glfwWindowShouldClose(win)) {
        if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        t.noClear = 0;
        if (mesh->frame_to) mesh->frame_to(mesh, &v, &t);
        if (vec->frame_to) vec->frame_to(vec, &v, &t);
        if (text) {
            // Панели: текст (имена/режимы/статус) + живой fps.
            // В ЦЕЛЬ (fbo), до презента — иначе презент перекроет.
            double now = glfwGetTime();
            if (now - tPrev >= 0.5) {
                tPrev = now;
                snprintf(fpsBuf, sizeof fpsBuf, "ui_test %d", frames);
            }
            ui_panels_text(g_panels, &g_scene, text, W, H);
            ui_text_quad(text, 14.0f, 700.0f, fpsBuf, white);
            glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            glViewport(0, 0, W, H);
            ui_text_flush(text, W, H);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (shot && shot[0] && frames == shotAt) {
            uint8_t *px = (uint8_t *)malloc((size_t)W * H * 3);
            if (px) {
                glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
                glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                FILE *ff = fopen(shot, "wb");
                if (ff) {
                    fprintf(ff, "P6\n%d %d\n255\n", W, H);
                    for (int y = H - 1; y >= 0; y--)
                        fwrite(px + (size_t)y * W * 3, 1, (size_t)W * 3, ff);
                    fclose(ff);
                    printf("ui_test: shot %s\n", shot);
                }
                free(px);
            }
        }
        comp_present_gl(prog, vao, &t, W, H);
        glfwSwapBuffers(win);
        glfwPollEvents();
        if (++frames == 120) printf("ui_test: 120 frames OK (seam alive)\n");
    }
    printf("ui_test: frames=%d DONE\n", frames);
    ui_panels_destroy(g_panels);
    ui_text_destroy(text);
    eng_ui_destroy(g_ui);
    return 0;
}
