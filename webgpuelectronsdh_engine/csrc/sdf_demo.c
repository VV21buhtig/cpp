// SDF-демо: окно + sdf-ядро + сцена из примитивов + орбита камеры.
// Не воксельное app (у него нет SDF-входа): минимальный фидер для проверки
// upload_sdf. --frames N для скриншот-прогона, VOX_CAM="yaw,pitch" фиксирует.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "core/render_api.h"

RenderCore *rc_sdf_create(void);

static void obj(RcSdfObj *o, int kind, float x0, float y0, float z0,
                float x1, float y1, float z1, float r, int mat) {
    o->kind = kind;
    o->p0.x = x0; o->p0.y = y0; o->p0.z = z0;
    o->p1.x = x1; o->p1.y = y1; o->p1.z = z1;
    o->r = r;
    o->mat = mat;
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(1280, 720, "sdf demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_sdf_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "sdf init fail\n"); return 1; }
    printf("core: %s caps=0x%x\n", rc->name, rc->caps);

    RcSdfObj scene[8];
    obj(&scene[0], 3, 0, 1, 0, 0, 0, 0, 0, 4); // земля y=0 серая
    obj(&scene[1], 0, -2.5f, 1.2f, 0, 0, 0, 0, 1.2f, 0); // сфера красная
    obj(&scene[2], 1, 0.5f, 1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0, 1); // короб зелёный
    obj(&scene[3], 2, 3.0f, 1.0f, 0.5f, 0.4f, 0, 0, 1.2f, 2); // тор синий R=1.2 r=0.4
    obj(&scene[4], 4, -0.5f, 2.5f, 2.5f, 1.5f, 3.5f, 2.5f, 0.3f, 3); // капсула жёлтая
    obj(&scene[5], 0, 2.5f, 0.6f, -3.0f, 0, 0, 0, 0.6f, 5); // сфера малая белая
    if (!rc->upload_sdf(rc, scene, 6)) { fprintf(stderr, "upload fail\n"); return 1; }

    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        RcView v;
        memset(&v, 0, sizeof v);
        // Орбита вокруг (0,1.5,0): радиус 9, высота 4. VOX_CAM фиксирует.
        double t = frame * 0.004;
        float cx = (float)(9.0 * cos(t)), cz = (float)(9.0 * sin(t));
        v.camPos.x = cx; v.camPos.y = 4.0f; v.camPos.z = cz;
        float dx = -cx, dy = 1.5f - 4.0f, dz = -cz;
        float dl = sqrtf(dx * dx + dy * dy + dz * dz);
        v.yaw = (float)atan2(dz, dx);
        v.pitch = (float)asin(dy / dl);
        v.fov = 1.6f;
        const char *ce = getenv("VOX_CAM");
        if (ce && ce[0]) {
            double yw, pt;
            if (sscanf(ce, "%lf,%lf", &yw, &pt) == 2) { v.yaw = (float)yw; v.pitch = (float)pt; }
            v.camPos.x = 9.0f; v.camPos.y = 4.0f; v.camPos.z = 0.0f; // детерминированная точка
        }
        int ww, hh;
        glfwGetFramebufferSize(win, &ww, &hh);
        v.resW = ww; v.resH = hh;
        v.sunDir.x = 0.4f; v.sunDir.y = 0.75f; v.sunDir.z = 0.3f;
        v.time = (float)frame / 60.0f;
        rc->frame(rc, &v);
        if (frame % 120 == 0)
            printf("f=%d fps=%.0f\n", frame, rc->fps(rc));
        frame++;
    }
    printf("done: %d frames\n", frame);
    rc->shutdown(rc);
    rc_destroy(rc);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
