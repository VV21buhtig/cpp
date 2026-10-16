// Ядро VECTOR: RenderCore API, caps=VECTOR. CPU-растр 2D-примитивов
// (rect/circle/line, src-over, порядок массива) + блит через RHI.
// Координаты пиксельные, начало СЛЕВА СВЕРХУ (экранная конвенция 2D).
// Первое ядро на RHI движка: контекст/программа — из rhi_gl, не напрямую.
// Бейдж циан.
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "render_api.h"
#include "rtarget.h"
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

// Треугольник kind=3: p0=(a,b), p1=(c,d), p2=(e,f). Barycentric fill по bbox
// (плоскости гизмо маленькие — быстро).
static void vec_tri(uint8_t *fb, int w, int h, float x0, float y0, float x1, float y1,
                    float x2, float y2, const float *rgba) {
    // Barycentric fill по bbox (плоскости гизмо маленькие — быстро).
    float minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1;
    if (x1 > maxx) maxx = x1;
    if (x2 < minx) minx = x2;
    if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1;
    if (y1 > maxy) maxy = y1;
    if (y2 < miny) miny = y2;
    if (y2 > maxy) maxy = y2;
    float den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    if (den > -1e-6f && den < 1e-6f) return;
    for (int y = (int)floorf(miny); y <= (int)ceilf(maxy); y++)
        for (int x = (int)floorf(minx); x <= (int)ceilf(maxx); x++) {
            float l0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den;
            float l1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den;
            float l2 = 1.0f - l0 - l1;
            if (l0 >= 0.0f && l1 >= 0.0f && l2 >= 0.0f)
                blend_px(fb, w, h, x, y, rgba);
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
        else if (shapes[i].kind == 3)
            vec_tri(p->fb, w, h, shapes[i].a, shapes[i].b, shapes[i].c, shapes[i].d,
                    shapes[i].e, shapes[i].f, shapes[i].rgba);
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

// Частичная заливка (dirty-rect): чистим и рисуем только регион, остальное fb
// не трогаем; в текстуру едет только регион (SubImage). bbox примитива мимо
// региона — пропуск (дешёвый reject до растра).
static int vec_upload_rect(RenderCore *rc, const RcVecShape *shapes, int n, int x, int y,
                           int w, int h) {
    VecWrap *p = (VecWrap *)rc->ctx;
    if (!p->fb || !shapes || n <= 0) return 0;
    int fw = p->fw, fh = p->fh;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > fw) w = fw - x;
    if (y + h > fh) h = fh - y;
    if (w <= 0 || h <= 0) return 0;
    if (n > 4096) n = 4096;
    // Чистим регион (прозрачный — было под стрелками стираем).
    for (int r = y; r < y + h; r++)
        memset(p->fb + ((size_t)r * fw + x) * 4, 0, (size_t)w * 4);
    for (int i = 0; i < n; i++) {
        // bbox примитива по kind.
        float bx0, by0, bx1, by1;
        if (shapes[i].kind == 0) {
            bx0 = shapes[i].a;
            by0 = shapes[i].b;
            bx1 = shapes[i].a + shapes[i].c;
            by1 = shapes[i].b + shapes[i].d;
        } else if (shapes[i].kind == 1) {
            bx0 = shapes[i].a - shapes[i].c;
            by0 = shapes[i].b - shapes[i].c;
            bx1 = shapes[i].a + shapes[i].c;
            by1 = shapes[i].b + shapes[i].c;
        } else if (shapes[i].kind == 2) {
            bx0 = shapes[i].a < shapes[i].c ? shapes[i].a : shapes[i].c;
            by0 = shapes[i].b < shapes[i].d ? shapes[i].b : shapes[i].d;
            bx1 = shapes[i].a > shapes[i].c ? shapes[i].a : shapes[i].c;
            by1 = shapes[i].b > shapes[i].d ? shapes[i].b : shapes[i].d;
        } else if (shapes[i].kind == 3) {
            bx0 = bx1 = shapes[i].a;
            by0 = by1 = shapes[i].b;
            if (shapes[i].c < bx0) bx0 = shapes[i].c;
            if (shapes[i].c > bx1) bx1 = shapes[i].c;
            if (shapes[i].d < by0) by0 = shapes[i].d;
            if (shapes[i].d > by1) by1 = shapes[i].d;
            if (shapes[i].e < bx0) bx0 = shapes[i].e;
            if (shapes[i].e > bx1) bx1 = shapes[i].e;
            if (shapes[i].f < by0) by0 = shapes[i].f;
            if (shapes[i].f > by1) by1 = shapes[i].f;
        } else {
            continue;
        }
        if (bx1 < x || by1 < y || bx0 > x + w || by0 > y + h) continue;
        if (shapes[i].kind == 0) vec_rect(p->fb, fw, fh, &shapes[i]);
        else if (shapes[i].kind == 1) vec_circle(p->fb, fw, fh, &shapes[i]);
        else if (shapes[i].kind == 2) vec_line(p->fb, fw, fh, &shapes[i]);
        else
            vec_tri(p->fb, fw, fh, shapes[i].a, shapes[i].b, shapes[i].c, shapes[i].d,
                    shapes[i].e, shapes[i].f, shapes[i].rgba);
    }
    // Флип только строк региона; саб-заливка с шагом строки = fw.
    static uint8_t *flip = 0;
    static int flipN = 0;
    if (fw * fh * 4 > flipN) {
        free(flip);
        flip = (uint8_t *)malloc((size_t)fw * fh * 4);
        flipN = flip ? fw * fh * 4 : 0;
    }
    if (!flip) return 0;
    for (int r = 0; r < h; r++)
        memcpy(flip + (size_t)(fh - 1 - (y + r)) * fw * 4 + (size_t)x * 4,
               p->fb + (size_t)(y + r) * fw * 4 + (size_t)x * 4, (size_t)w * 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, fw);
    glBindTexture(GL_TEXTURE_2D, p->blit.tex);
    int ok = blit_upload_rect(&p->blit, flip + (size_t)(fh - y - h) * fw * 4 + (size_t)x * 4,
                              x, fh - y - h, w, h);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    return ok;
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

// Кадр в чужую цель поверх (бленд): текстура уже залита в feed.
// Без чистки/swap/бейджа. Размеры fb и цели должны совпадать (демо следит).
static int vec_frame_to(RenderCore *rc, const RcView *v, RcTarget *t) {
    VecWrap *p = (VecWrap *)rc->ctx;
    (void)v;
    if (!t || t->backend != 2 || !t->fbo || !p->blit.tex) return 0;
    blit_overlay_to(&p->blit, t->fbo, t->w, t->h);
    return 1;
}

static int vec_init(RenderCore *rc, void *glfwWindow) {
    VecWrap *p = (VecWrap *)rc->ctx;
    if (!blit_init(&p->blit, (GLFWwindow *)glfwWindow)) return 0;
    printf("vec core OK: cpu raster on RHI\n");
    return 1;
}

static void vec_shutdown(RenderCore *rc) {
    VecWrap *p = (VecWrap *)rc->ctx;
    blit_shutdown(&p->blit);
    free(p->fb);
    p->fb = 0;
}

#ifdef __GNUC__
__attribute__((constructor))
#endif
static void rc_reg_self(void) {
    rc_register("vec", RC_CAP_VECTOR, RC_BACKEND_GL, rc_vec_create);
}

// Горячая замена шейдера блита (Monaco).
static int core_shader_reload(RenderCore *rc, const char *id, const char *code, char *log,
                              int logcap) {
    VecWrap *p = (VecWrap *)rc->ctx;
    return blit_reload(&p->blit, id, code, log, logcap);
}

RenderCore *rc_vec_create(void) {
    VecWrap *w = (VecWrap *)calloc(1, sizeof *w);
    if (!w) return 0;
    w->api.ctx = w;
    w->api.caps = RC_CAP_VECTOR;
    w->api.name = "vec";
    w->api.backend = RC_BACKEND_GL;
    w->api.init = vec_init;
    w->api.shutdown = vec_shutdown;
    w->api.frame = vec_frame;
    w->api.frame_to = vec_frame_to;
    w->api.upload_vector = vec_upload;
    w->api.upload_vector_rect = vec_upload_rect;
    w->api.fps = vec_fps;
    w->api.shader_reload = core_shader_reload;
    return &w->api;
}
