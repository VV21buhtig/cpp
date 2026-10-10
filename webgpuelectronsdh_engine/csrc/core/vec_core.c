// Ядро VECTOR: RenderCore API, caps=VECTOR. CPU-растр 2D-примитивов
// (rect/circle/line, src-over, порядок массива) + блит через gl_blit.
// Координаты пиксельные, начало СЛЕВА СВЕРХУ (экранная конвенция 2D).
// Бейдж циан.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"
#include "gl_blit.h"

typedef struct {
    RenderCore api;
    BlitGL blit;
    uint8_t *fb; // верх вниз, w*h*4
    int fw, fh;
} VecWrap;

static float vec_fps(RenderCore *rc) {
    return ((VecWrap *)rc->ctx)->blit.fpsEma;
}

static void blend_px(uint8_t *fb, int w, int h, int x, int y, const float *rgba) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint8_t *p = fb + ((size_t)y * w + x) * 4;
    float sa = rgba[3] < 0 ? 0 : (rgba[3] > 1 ? 1 : rgba[3]);
    for (int i = 0; i < 3; i++) {
        float dc = p[i] / 255.0f;
        float sc = rgba[i] < 0 ? 0 : (rgba[i] > 1 ? 1 : rgba[i]);
        p[i] = (uint8_t)((sc * sa + dc * (1.0f - sa)) * 255.0f + 0.5f);
    }
    float da = p[3] / 255.0f;
    p[3] = (uint8_t)((sa + da * (1.0f - sa)) * 255.0f + 0.5f);
}

static void vec_rect(uint8_t *fb, int w, int h, const RcVecShape *s) {
    int x0 = (int)floorf(s->a), y0 = (int)floorf(s->b);
    int x1 = (int)ceilf(s->a + s->c), y1 = (int)ceilf(s->b + s->d);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) blend_px(fb, w, h, x, y, s->rgba);
}

static void vec_circle(uint8_t *fb, int w, int h, const RcVecShape *s) {
    // a,b центр, c радиус. Заливка по bbox с проверкой дистанции.
    int x0 = (int)floorf(s->a - s->c), y0 = (int)floorf(s->b - s->c);
    int x1 = (int)ceilf(s->a + s->c), y1 = (int)ceilf(s->b + s->c);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = (x + 0.5f) - s->a, dy = (y + 0.5f) - s->b;
            if (dx * dx + dy * dy <= s->c * s->c) blend_px(fb, w, h, x, y, s->rgba);
        }
}

static void vec_line(uint8_t *fb, int w, int h, const RcVecShape *s) {
    // a,b,c,d = x0,y0,x1,y1. Толщина 1px, шаги по длине.
    float x0 = s->a, y0 = s->b, x1 = s->c, y1 = s->d;
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(sqrtf(dx * dx + dy * dy) + 0.5f);
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        blend_px(fb, w, h, (int)(x0 + dx * t), (int)(y0 + dy * t), s->rgba);
    }
}

static int vec_upload(RenderCore *rc, const RcVecShape *shapes, int n) {
    VecWrap *p = (VecWrap *)rc->ctx;
    if (!p->fb) { // upload раньше первого frame: дефолт, frame поправит
        p->fb = (uint8_t *)malloc((size_t)1280 * 720 * 4);
        if (!p->fb) return 0;
        p->fw = 1280;
        p->fh = 720;
    }
    int w = p->fw, h = p->fh;
    if (!shapes || n <= 0) return 0;
    if (n > 4096) n = 4096;
    // Фон: тёмный. Порядок массива = порядок рисования.
    memset(p->fb, 0, (size_t)w * h * 4);
    for (int i = 0; i < n; i++) {
        if (shapes[i].kind == 0) vec_rect(p->fb, w, h, &shapes[i]);
        else if (shapes[i].kind == 1) vec_circle(p->fb, w, h, &shapes[i]);
        else if (shapes[i].kind == 2) vec_line(p->fb, w, h, &shapes[i]);
    }
    // Верх вниз -> низ вверх под блит.
    static uint8_t *flip = 0;
    static int flipN = 0;
    if (w * h * 4 > flipN) {
        free(flip);
        flip = (uint8_t *)malloc((size_t)w * h * 4);
        flipN = flip ? w * h * 4 : 0;
    }
    if (!flip) return 0;
    for (int y = 0; y < h; y++)
        memcpy(flip + (size_t)y * w * 4, p->fb + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    return blit_upload(&p->blit, flip, w, h);
}

static void vec_frame(RenderCore *rc, const RcView *v) {
    VecWrap *p = (VecWrap *)rc->ctx;
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    if (ww != p->fw || hh != p->fh) {
        free(p->fb);
        p->fb = (uint8_t *)malloc((size_t)ww * hh * 4);
        if (p->fb) { p->fw = ww; p->fh = hh; }
        else { p->fw = p->fh = 0; }
    }
    blit_frame(&p->blit, ww, hh, 0.0f, 1.0f, 1.0f); // бейдж циан
}

static int vec_init(RenderCore *rc, void *glfwWindow) {
    VecWrap *p = (VecWrap *)rc->ctx;
    if (!blit_init(&p->blit, (GLFWwindow *)glfwWindow)) return 0;
    printf("vec core OK: cpu raster\n");
    return 1;
}

static void vec_shutdown(RenderCore *rc) {
    VecWrap *p = (VecWrap *)rc->ctx;
    blit_shutdown(&p->blit);
    free(p->fb);
    p->fb = 0;
}

RenderCore *rc_vec_create(void) {
    VecWrap *w = (VecWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VECTOR | RC_WINDOW_GL;
    w->api.name = "vec";
    w->api.init = vec_init;
    w->api.shutdown = vec_shutdown;
    w->api.frame = vec_frame;
    w->api.upload_vector = vec_upload;
    w->api.fps = vec_fps;
    return &w->api;
}
