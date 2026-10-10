// Слепой цикл движка: окно, ввод, кап. C11, только GLFW + render_api.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine_api.h"

struct EngEngine {
    RenderCore *rc;
    GLFWwindow *win;
    EngHandlers h;
    unsigned char keys[512];
    double lastMX, lastMY, mdx, mdy, scroll;
    int btn[8];
    int frames;
};

static void on_key(GLFWwindow *w, int key, int sc, int act, int mods) {
    (void)sc; (void)mods;
    EngEngine *e = (EngEngine *)glfwGetWindowUserPointer(w);
    if (!e || key < 0 || key >= 512) return;
    if (act == GLFW_PRESS) e->keys[key] = 1;
    else if (act == GLFW_RELEASE) e->keys[key] = 0;
}

static void on_btn(GLFWwindow *w, int b, int act, int mods) {
    (void)mods;
    EngEngine *e = (EngEngine *)glfwGetWindowUserPointer(w);
    if (!e || b < 0 || b >= 8) return;
    e->btn[b] = (act == GLFW_PRESS) ? 1 : 0;
}

static void on_scroll(GLFWwindow *w, double x, double y) {
    (void)x;
    EngEngine *e = (EngEngine *)glfwGetWindowUserPointer(w);
    if (e) e->scroll += y;
}

EngEngine *eng_create(RenderCore *rc, int w, int h, const char *title,
                      const EngHandlers *hh) {
    if (!rc || !rc->frame || !rc->init) return 0;
    if (!glfwInit()) return 0;
    if (rc->backend == RC_BACKEND_GL) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    GLFWwindow *win = glfwCreateWindow(w > 0 ? w : 1280, h > 0 ? h : 720,
                                       title ? title : "engine", 0, 0);
    if (!win) { glfwTerminate(); return 0; }
    EngEngine *e = (EngEngine *)calloc(1, sizeof *e);
    if (!e) { glfwDestroyWindow(win); glfwTerminate(); return 0; }
    e->rc = rc;
    e->win = win;
    if (hh) e->h = *hh;
    glfwSetWindowUserPointer(win, e);
    glfwSetKeyCallback(win, on_key);
    glfwSetMouseButtonCallback(win, on_btn);
    glfwSetScrollCallback(win, on_scroll);
    glfwGetCursorPos(win, &e->lastMX, &e->lastMY);
    if (!rc->init(rc, win)) { eng_destroy(e); return 0; }
    char t[160];
    snprintf(t, sizeof t, "%s [%s]", title ? title : "engine",
             rc->name ? rc->name : "?");
    glfwSetWindowTitle(win, t);
    return e;
}

void eng_destroy(EngEngine *e) {
    if (!e) return;
    if (e->rc && e->rc->shutdown) e->rc->shutdown(e->rc);
    if (e->win) glfwDestroyWindow(e->win);
    glfwTerminate();
    free(e);
}

int eng_frame(EngEngine *e) {
    if (!e || glfwWindowShouldClose(e->win)) return 1;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    glfwPollEvents();
    double mx, my;
    glfwGetCursorPos(e->win, &mx, &my);
    e->mdx = mx - e->lastMX;
    e->mdy = my - e->lastMY;
    e->lastMX = mx;
    e->lastMY = my;
    EngInput in;
    in.keys = e->keys;
    in.mouseDX = e->mdx;
    in.mouseDY = e->mdy;
    memcpy(in.mouseBtn, e->btn, sizeof e->btn);
    in.scroll = e->scroll;
    e->scroll = 0;
    if (e->h.input) e->h.input(e->h.ctx, &in);
    RcView v;
    memset(&v, 0, sizeof v);
    int ww, hh;
    glfwGetFramebufferSize(e->win, &ww, &hh);
    v.resW = ww;
    v.resH = hh;
    if (e->h.view) e->h.view(e->h.ctx, &v);
    if (e->h.feed) e->h.feed(e->h.ctx, e->rc);
    e->rc->frame(e->rc, &v);
    e->frames++;
    // Кап 60fps.
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long dt = (t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);
    long want = 16666666L;
    if (dt < want) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = want - dt;
        nanosleep(&ts, 0);
    }
    return glfwWindowShouldClose(e->win) ? 1 : 0;
}

int eng_run(EngEngine *e) {
    if (!e) return 0;
    while (!eng_frame(e)) {}
    int n = e->frames;
    return n;
}
