// Пиксельное демо: CPU-кадр (градиент + бегущий квадрат) -> pix-ядро.
// --frames N для прогона, VOX_SHOT работает (тот же протокол).
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render_api.h"

RenderCore *rc_pix_create(void);

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(640, 360, "pix demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_pix_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "pix init fail\n"); return 1; }
    printf("core: %s caps=0x%x\n", rc->name, rc->caps);
    static uint8_t px[640 * 360 * 4];
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        // Градиент + бегущий белый квадрат 32x32 (строка 0 = верх).
        for (int y = 0; y < 360; y++)
            for (int x = 0; x < 640; x++) {
                uint8_t *p = px + ((size_t)y * 640 + x) * 4;
                p[0] = (uint8_t)(x * 255 / 639);
                p[1] = (uint8_t)(y * 255 / 359);
                p[2] = 128;
                p[3] = 255;
            }
        int bx = (frame * 4) % (640 - 32), by = 100 + (int)(60.0 * sin(frame * 0.05));
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                uint8_t *p = px + ((size_t)(by + y) * 640 + bx + x) * 4;
                p[0] = p[1] = p[2] = 255;
                p[3] = 255;
            }
        if (!rc->upload_pixels(rc, px, 640, 360)) { fprintf(stderr, "upload fail\n"); return 1; }
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
