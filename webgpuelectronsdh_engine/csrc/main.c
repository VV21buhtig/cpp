// Рендер на C: весь CPU-кадр (камера orbit + guard + UBO ring + stage/flush).
// GPU-submit (Dawn queue.writeBuffer + draw) — следующая веха, когда будет Dawn;
// точка ухода одна: sdf_gpu_stage(), сейчас стейджим в память.
// Шаг 1 собирается штатно: cmake --build.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sdf_math.h"
#include "sdf_arena.h"
#include "sdf_ubo.h"
#include "sdf_scene.h"
#include "sdf_gpu.h"

#define FRAMES_IN_FLIGHT 2

static double g_yaw = -0.6, g_pitch = 0.25, g_dist = 7.0;
static double g_lx, g_ly; static int g_drag = 0;

static void on_mouse(GLFWwindow *w, double x, double y) {
    (void)w;
    if (!g_drag) { g_lx = x; g_ly = y; return; }
    g_yaw -= (x - g_lx) * 0.005;
    // Низ -0.12: ниже камера уходит под бесконечную плоскость (внутрь),
    // дальше guard вытянет. Порт renderer.js.
    g_pitch += (y - g_ly) * 0.005;
    if (g_pitch > 1.45) g_pitch = 1.45;
    if (g_pitch < -0.12) g_pitch = -0.12;
    g_lx = x; g_ly = y;
}
static void on_btn(GLFWwindow *w, int b, int act, int m) {
    (void)w; (void)m;
    if (b == GLFW_MOUSE_BUTTON_LEFT) g_drag = (act == GLFW_PRESS);
}
static void on_scroll(GLFWwindow *w, double dx, double dy) {
    (void)w; (void)dx;
    g_dist *= (1.0 + (dy > 0 ? -0.1 : dy < 0 ? 0.1 : 0.0));
    if (g_dist < 2.5) g_dist = 2.5;
    if (g_dist > 30.0) g_dist = 30.0;
}

int main(int argc, char **argv) {
    int maxFrames = -1;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);

    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    GLFWwindow *win = glfwCreateWindow(1280, 720, "sdf engine — C render (CPU side)", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    glfwSetCursorPosCallback(win, on_mouse);
    glfwSetMouseButtonCallback(win, on_btn);
    glfwSetScrollCallback(win, on_scroll);

    static uint8_t frameMem[64 * 1024];
    Arena frameArena; arena_init(&frameArena, frameMem, sizeof frameMem);

    // Ring UBO на FRAMES_IN_FLIGHT: пока GPU нет — стейджинг в память.
    // Тут (Vega UMA) когерентно: memcpy достаточно.
    // Там (дискретка, non-coherent map): + flush. См. sdf_gpu.h.
    static SdfUBO uboMirror[FRAMES_IN_FLIGHT];
    static uint8_t uboStaged[FRAMES_IN_FLIGHT][64];
    memset(uboMirror, 0, sizeof uboMirror);
    const SdfMemKind memKind = SDF_MEM_COHERENT; // Vega UMA; на RTX переключить в NONCOHERENT+flush

    Vec3 target = v3(0.0f, 1.0f, 0.0f);
    int frame = 0;
    double t0 = glfwGetTime();
    double lastLog = -10.0; // первый лог сразу, дальше раз в 4с (не спамить)
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        arena_reset(&frameArena); // 0 malloc в лупе
        int fi = frame % FRAMES_IN_FLIGHT;

        double t = glfwGetTime() - t0;
        Vec3 pos; sdf_camera_orbit(target, (float)g_yaw, (float)g_pitch, (float)g_dist, &pos);
        float cx = pos.x, cy = pos.y, cz = pos.z;
        sdf_guard(&cx, &cy, &cz, target.x, target.y, target.z);
        int ww, hh; glfwGetFramebufferSize(win, &ww, &hh);

        SdfUBO *u = &uboMirror[fi];
        u->camPos = v3(cx, cy, cz); u->time = (float)t;
        u->camTarget = target;      u->resX = (float)ww;
        u->sunDir = sdf_sun((float)t); u->maxSteps = 100.0f;
        u->resY = (float)hh;        u->pad[0] = u->pad[1] = u->pad[2] = 0.0f;
        int needFlush = sdf_ubo_stage(uboStaged[fi], u);
        if (needFlush && memKind == SDF_MEM_NONCOHERENT) {
            // flush mapping (Vulkan non-coherent) — на Vega ветка не выполняется
        }

        if (t - lastLog >= 4.0) {
            lastLog = t;
            printf("f=%d fi=%d pos=(%.2f,%.2f,%.2f) res=%dx%d arena_off=%zu ubo=%zuB\n",
                frame, fi, cx, cy, cz, ww, hh, frameArena.off, sizeof(SdfUBO));
        }

        glfwSwapBuffers(win);
        if (++frame == maxFrames) break;
    }
    printf("render-C OK: %d frames, ubo=%zuB x%d\n", frame, sizeof(SdfUBO), FRAMES_IN_FLIGHT);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
