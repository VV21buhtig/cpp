// Тест базы: движок крутит цикл с нулевым ядром (без GPU, без контента).
// Проверяет окно/ввод/кап, а не картинку.
#include <stdio.h>
#include <string.h>
#include "engine_api.h"

static int g_frames = 0;
static int g_fed = 0;

static int null_init(RenderCore *rc, void *win) {
    (void)rc; (void)win;
    return 1;
}
static void null_shutdown(RenderCore *rc) {
    (void)rc;
}
static void null_frame(RenderCore *rc, const RcView *v) {
    (void)rc;
    if (v->resW <= 0 || v->resH <= 0) {
        fprintf(stderr, "ENG FAIL: bad res\n");
    }
    g_frames++;
}
static float null_fps(RenderCore *rc) {
    (void)rc;
    return 60.0f;
}
static void t_view(void *ctx, RcView *v) {
    (void)ctx;
    v->fov = 1.6f;
    v->sunDir.x = 0.0f;
    v->sunDir.y = 1.0f;
    v->sunDir.z = 0.0f;
}
static void t_feed(void *ctx, RenderCore *rc) {
    (void)ctx; (void)rc;
    g_fed++;
}

int main(void) {
    static RenderCore nullRc;
    memset(&nullRc, 0, sizeof nullRc);
    nullRc.caps = 0; // NO_API окно, GPU не трогаем
    nullRc.name = "null";
    nullRc.init = null_init;
    nullRc.shutdown = null_shutdown;
    nullRc.frame = null_frame;
    nullRc.fps = null_fps;
    EngHandlers h;
    memset(&h, 0, sizeof h);
    h.view = t_view;
    h.feed = t_feed;
    EngEngine *e = eng_create(&nullRc, 320, 200, "engtest", &h);
    if (!e) { printf("ENG FAIL: create\n"); return 1; }
    for (int i = 0; i < 10; i++)
        if (eng_frame(e)) break;
    eng_destroy(e);
    printf("frames=%d fed=%d (want 10/10)\n", g_frames, g_fed);
    if (g_frames != 10 || g_fed != 10) { printf("ENG FAIL\n"); return 1; }
    printf("ENG OK\n");
    return 0;
}
