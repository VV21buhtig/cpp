// Общая 2D-сцена движка: ОДНА на всех (вместо демок под каждое ядро).
// Пока ест только vec-ядро (фигуры); pix/sand/lua остаются отдельными
// витринами доменов, не сценами.
// --core vec (другим тут делать нечего — честный отказ), --frames N.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"

static void shape(RcVecShape *s, int kind, float a, float b, float c, float d,
                  float r, float g, float bl, float al) {
    s->kind = kind;
    s->a = a; s->b = b; s->c = c; s->d = d;
    s->rgba[0] = r; s->rgba[1] = g; s->rgba[2] = bl; s->rgba[3] = al;
}

int main(int argc, char **argv) {
    const char *coreName = "vec";
    int maxFrames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--core") && i + 1 < argc) coreName = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    }
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    RenderCore *rc = rc_create(coreName);
    if (!rc) { fprintf(stderr, "no core '%s'\n", coreName); return 1; }
    if (!(rc->caps & RC_CAP_VECTOR) || !rc->upload_vector) {
        fprintf(stderr, "core '%s' has no vector input\n", rc->name);
        return 1;
    }
    if (rc->backend == RC_BACKEND_GL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    GLFWwindow *win = glfwCreateWindow(640, 360, "scene2d", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    if (!rc->init(rc, win)) { fprintf(stderr, "core init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        RcVecShape sh[6];
        shape(&sh[0], 0, 20, 20, 600, 320, 0.8f, 0.8f, 0.8f, 1.0f);
        shape(&sh[1], 2, 20, 20, 620, 340, 1.0f, 0.2f, 0.2f, 1.0f);
        float cx = 320.0f + 200.0f * (float)cos(frame * 0.03);
        float cy = 180.0f + 100.0f * (float)sin(frame * 0.05);
        shape(&sh[2], 1, cx, cy, 40, 0, 0.2f, 0.6f, 1.0f, 1.0f);
        shape(&sh[3], 0, cx - 50, cy - 50, 100, 100, 1.0f, 1.0f, 0.0f, 0.5f);
        shape(&sh[4], 2, 20, 340, 620, 20, 0.2f, 1.0f, 0.2f, 1.0f);
        shape(&sh[5], 1, 100, 280, 18, 0, 1.0f, 0.5f, 0.0f, 0.8f);
        if (!rc->upload_vector(rc, sh, 6)) { fprintf(stderr, "upload fail\n"); return 1; }
        RcView v;
        memset(&v, 0, sizeof v);
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        v.resW = ww;
        v.resH = hh;
        rc->frame(rc, &v);
        if (frame % 60 == 0) printf("f=%d fps=%.0f\n", frame, rc->fps(rc));
        frame++;
    }
    printf("done: %d frames\n", frame);
    rc->shutdown(rc);
    rc_destroy(rc);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
