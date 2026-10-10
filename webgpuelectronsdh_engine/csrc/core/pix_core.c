// Ядро PIXEL: RenderCore API, caps=PIXEL. CPU-кадр RGBA целиком за раз.
// Рендера нет — только приём кадра (строка 0 = верх) и блит через gl_blit.
// Бейдж жёлтый.
#include <stdlib.h>
#include <string.h>
#include "render_api.h"
#include "gl_blit.h"

typedef struct {
    RenderCore api;
    BlitGL blit;
    uint8_t *fb; // копия последнего кадра (низом вверх), w*h*4
    int fw, fh;
    int has;
} PixWrap;

static float pix_fps(RenderCore *rc) {
    return ((PixWrap *)rc->ctx)->blit.fpsEma;
}

static int pix_upload(RenderCore *rc, const uint8_t *rgba, int w, int h) {
    PixWrap *p = (PixWrap *)rc->ctx;
    if (!rgba || w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
    size_t n = (size_t)w * h * 4;
    if (w != p->fw || h != p->fh) {
        free(p->fb);
        p->fb = (uint8_t *)malloc(n);
        if (!p->fb) { p->fw = p->fh = 0; return 0; }
        p->fw = w;
        p->fh = h;
    }
    // Верх вниз -> низ вверх (GL-порядок).
    for (int y = 0; y < h; y++)
        memcpy(p->fb + (size_t)y * w * 4, rgba + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    p->has = blit_upload(&p->blit, p->fb, w, h);
    return p->has;
}

static void pix_frame(RenderCore *rc, const RcView *v) {
    PixWrap *p = (PixWrap *)rc->ctx;
    int ww = v->resW > 0 ? v->resW : 1280;
    int hh = v->resH > 0 ? v->resH : 720;
    blit_frame(&p->blit, ww, hh, 1.0f, 1.0f, 0.0f); // бейдж жёлтый
}

static int pix_init(RenderCore *rc, void *glfwWindow) {
    PixWrap *p = (PixWrap *)rc->ctx;
    // НЕ memset'им всё: caps/name/указатели уже стоят из rc_pix_create.
    if (!blit_init(&p->blit, (GLFWwindow *)glfwWindow)) return 0;
    printf("pix core OK: blit\n");
    return 1;
}

static void pix_shutdown(RenderCore *rc) {
    PixWrap *p = (PixWrap *)rc->ctx;
    blit_shutdown(&p->blit);
    free(p->fb);
    p->fb = 0;
}

RenderCore *rc_pix_create(void) {
    PixWrap *w = (PixWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_PIXEL | RC_WINDOW_GL;
    w->api.name = "pix";
    w->api.init = pix_init;
    w->api.shutdown = pix_shutdown;
    w->api.frame = pix_frame;
    w->api.upload_pixels = pix_upload;
    w->api.fps = pix_fps;
    return &w->api;
}
