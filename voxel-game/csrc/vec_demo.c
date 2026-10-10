// Векторное демо: примитивы (круг в полёте + рамки + диагональ) -> vec-ядро.
// --frames N для прогона, VOX_SHOT работает (тот же протокол).
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"

RenderCore *rc_vec_create(void);

static void shape(RcVecShape *s, int kind, float a, float b, float c, float d,
                  float r, float g, float bl, float al) {
    s->kind = kind;
    s->a = a; s->b = b; s->c = c; s->d = d;
    s->rgba[0] = r; s->rgba[1] = g; s->rgba[2] = bl; s->rgba[3] = al;
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(640, 360, "vec demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_vec_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "vec init fail\n"); return 1; }
    printf("core: %s caps=0x%x\n", rc->name, rc->caps);
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        RcVecShape sh[6];
        // Рамка, диагональ, летящий круг, полупрозрачный квадрат поверх.
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
