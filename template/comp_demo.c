// Композитное демо L3: воксель-сцена + векторный UI в ОДНОМ кадре.
// Проход 0 (gl, база): чистит цель и рисует террейн.
// Проход 1 (vec, UI): рамка + бегущая точка поверх, блендом, без чистки.
// Показ цели на экран + шот — здесь (демо), не в ядрах.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "render_api.h"
#include "rtarget.h"
#include "comp.h"
#include "comp_gl.h"
#include "rhi_gl.h"
#include "vox/vox_chunk.h"
#include "vox/vox_gen.h"
#include "vox_feed.h"

RenderCore *rc_gl_create(void);
RenderCore *rc_vec_create(void);

#define GW 5

static VoxChunk world[GW][GW];

static const VoxChunk *findc(int cx, int cz) {
    int ix = cx + GW / 2, iz = cz + GW / 2;
    if (ix < 0 || iz < 0 || ix >= GW || iz >= GW) return 0;
    return &world[ix][iz];
}

typedef struct {
    RenderCore *rc;
    int upDone[GW][GW];
    int upLeft;
    int cur;
    int frame;
} VoxFeedCtx;

static void vox_view(void *ctx, RcView *v) {
    VoxFeedCtx *f = (VoxFeedCtx *)ctx;
    double t = f->frame * 0.004;
    v->camPos.x = 8 + (float)(90.0 * cos(t));
    v->camPos.y = 66.0f;
    v->camPos.z = 8 + (float)(90.0 * sin(t));
    float dx = -90.0f * (float)cos(t), dy = 26.0f - 66.0f, dz = -90.0f * (float)sin(t);
    float dl = sqrtf(dx * dx + dy * dy + dz * dz);
    v->yaw = (float)atan2(dz, dx);
    v->pitch = (float)asin(dy / dl);
    v->fov = 1.6f;
    v->resW = 1280;
    v->resH = 720;
    v->sunDir.x = 0.4f; v->sunDir.y = 0.75f; v->sunDir.z = 0.3f;
    v->time = (float)f->frame / 60.0f;
}

static void vox_feed(void *ctx, RenderCore *rc) {
    VoxFeedCtx *f = (VoxFeedCtx *)ctx;
    for (int k = 0; k < 12 && f->upLeft > 0; k++) {
        while (f->cur < GW * GW && f->upDone[f->cur % GW][f->cur / GW]) f->cur++;
        if (f->cur >= GW * GW) { f->upLeft = 0; break; }
        int cx = f->cur % GW, cz = f->cur / GW;
        int wx = cx - GW / 2, wz = cz - GW / 2;
        const VoxChunk *nb[6] = {
            findc(wx - 1, wz), findc(wx + 1, wz), 0, 0,
            findc(wx, wz - 1), findc(wx, wz + 1),
        };
        if (vox_feed_one(rc, &world[cx][cz], nb) >= 0) {
            f->upDone[cx][cz] = 1;
            f->upLeft--;
        }
        f->cur++;
    }
}

typedef struct {
    int frame;
} UiCtx;

static void ui_view(void *ctx, RcView *v) {
    (void)ctx;
    v->resW = 1280;
    v->resH = 720;
}

static void ui_feed(void *ctx, RenderCore *rc) {
    UiCtx *u = (UiCtx *)ctx;
    RcVecShape sh[3];
    // Верхняя кромка + прицел-диагональ + летящая точка (жёлтый UI).
    sh[0].kind = 2;
    sh[0].a = 8; sh[0].b = 8; sh[0].c = 1272; sh[0].d = 8;
    // Диагональ-прицел через летящую точку.
    float px = 640.0f + 500.0f * (float)cos(u->frame * 0.02);
    float py = 360.0f + 280.0f * (float)sin(u->frame * 0.03);
    sh[1].kind = 2;
    sh[1].a = 640; sh[1].b = 360; sh[1].c = px; sh[1].d = py;
    sh[2].kind = 1;
    sh[2].a = px; sh[2].b = py; sh[2].c = 14; sh[2].d = 0;
    for (int i = 0; i < 3; i++) {
        sh[i].rgba[0] = 1.0f; sh[i].rgba[1] = 1.0f;
        sh[i].rgba[2] = 0.0f; sh[i].rgba[3] = 0.9f;
    }
    rc->upload_vector(rc, sh, 3);
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "comp demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *gl = rc_gl_create();
    RenderCore *vec = rc_vec_create();
    if (!gl || !vec || !gl->init(gl, win)) { fprintf(stderr, "core init fail\n"); return 1; }
    // ОБА ядра на одном окне/контексте: vec делит контекст gl (make current).
    if (!vec->init(vec, win)) { fprintf(stderr, "vec init fail\n"); return 1; }
    printf("comp: %s + %s\n", gl->name, vec->name);
    for (int cz = 0; cz < GW; cz++)
        for (int cx = 0; cx < GW; cx++) {
            world[cx][cz].cx = cx - GW / 2;
            world[cx][cz].cz = cz - GW / 2;
            vox_gen(&world[cx][cz], 1337);
        }
    RcTarget target;
    if (!comp_target_gl_new(&target, 1280, 720)) return 1;
    unsigned presentProg = rhi_gl_prog("engine/shaders/blit/blit.vert",
                                       "engine/shaders/blit/blit.frag");
    unsigned presentVao = rhi_gl_empty_vao();
    VoxFeedCtx vf;
    memset(&vf, 0, sizeof vf);
    vf.rc = gl;
    vf.upLeft = GW * GW;
    UiCtx ui;
    memset(&ui, 0, sizeof ui);
    EngPass passes[2] = {
        {gl, &vf, vox_view, vox_feed},
        {vec, &ui, ui_view, ui_feed},
    };
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        vf.frame = frame;
        ui.frame = frame;
        int rc_ = eng_comp_frame(passes, 2, &target);
        if (rc_ != ENG_COMP_OK) { fprintf(stderr, "comp fail %d\n", rc_); return 1; }
        comp_present_gl(presentProg, presentVao, &target, 1280, 720);
        // Шот готового кадра.
        {
            const char *sp = getenv("VOX_SHOT");
            if (sp && sp[0]) {
                const char *sa = getenv("VOX_SHOT_AT");
                int at = sa && sa[0] ? atoi(sa) : 60;
                if (frame == at) {
                    uint8_t *px = (uint8_t *)malloc((size_t)1280 * 720 * 3);
                    if (px) {
                        glReadPixels(0, 0, 1280, 720, GL_RGB, GL_UNSIGNED_BYTE, px);
                        FILE *f = fopen(sp, "wb");
                        if (f) {
                            fprintf(f, "P6\n1280 720\n255\n");
                            for (int y = 719; y >= 0; y--)
                                fwrite(px + (size_t)y * 1280 * 3, 1, (size_t)1280 * 3, f);
                            fclose(f);
                            fprintf(stderr, "shot: %s\n", sp);
                        }
                        free(px);
                    }
                }
            }
        }
        glfwSwapBuffers(win);
        if (frame % 60 == 0) printf("f=%d fps=%.0f\n", frame, gl->fps(gl));
        frame++;
    }
    printf("done: %d frames\n", frame);
    comp_target_gl_free(&target);
    gl->shutdown(gl);
    vec->shutdown(vec);
    rc_destroy(gl);
    rc_destroy(vec);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
