// Шаг 1: каркас C11 + окно GLFW, без GPU.
// Собирается СЕГОДНЯ (gcc+glfw есть). Dawn/WebGPU придут следующим коммитом:
// натив — webgpu_dawn, веб — emcmake + emdawnwebgpu (emsdk пока нет).
// В лупе: 0 malloc, арена reset, UBO-зеркало заполняется как vulcan frame.cpp,
// но раздельные данные на кадр (анти-урок общего indBuf).
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sdf_math.h"
#include "sdf_arena.h"
#include "sdf_ubo.h"

static double g_yaw = -0.6, g_pitch = 0.25, g_dist = 7.0;
static double g_lx, g_ly; static int g_drag = 0;

static void on_mouse(GLFWwindow *w, double x, double y) {
    (void)w;
    if (!g_drag) { g_lx = x; g_ly = y; return; }
    g_yaw -= (x - g_lx) * 0.005;
    g_pitch += (y - g_ly) * 0.005;
    if (g_pitch > 1.4) g_pitch = 1.4;
    if (g_pitch < -0.2) g_pitch = -0.2;
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
    GLFWwindow *win = glfwCreateWindow(1280, 720, "sdf engine — step1 (no GPU yet)", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    glfwSetCursorPosCallback(win, on_mouse);
    glfwSetMouseButtonCallback(win, on_btn);
    glfwSetScrollCallback(win, on_scroll);

    static uint8_t frameMem[64 * 1024];
    Arena frameArena; arena_init(&frameArena, frameMem, sizeof frameMem);

    SdfUBO uboMirror; // зеркало как FrameUBO в vulcan, но 64Б
    memset(&uboMirror, 0, sizeof uboMirror);

    Vec3 target = v3(0.0f, 1.0f, 0.0f);
    int frame = 0;
    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        arena_reset(&frameArena); // весь временный мусор кадра — сюда, 0 malloc

        double t = glfwGetTime() - t0;
        Vec3 pos; sdf_camera_orbit(target, (float)g_yaw, (float)g_pitch, (float)g_dist, &pos);
        int ww, hh; glfwGetFramebufferSize(win, &ww, &hh);

        uboMirror.camPos = pos;      uboMirror.time = (float)t;
        uboMirror.camTarget = target; uboMirror.resX = (float)ww;
        uboMirror.sunDir = sdf_sun((float)t); uboMirror.maxSteps = 100.0f;
        uboMirror.resY = (float)hh;

        // Следующий коммит: memcpy -> queue.writeBuffer(ubo) + draw(3).
        // Сейчас проверяем математику: раз в 10 секунд печатаем.
        if (frame % 600 == 0)
            printf("f=%d pos=(%.2f,%.2f,%.2f) res=%dx%d sun=(%.2f,%.2f,%.2f) arena_off=%zu ubo=%zuB\n",
                frame, pos.x, pos.y, pos.z, ww, hh,
                uboMirror.sunDir.x, uboMirror.sunDir.y, uboMirror.sunDir.z,
                frameArena.off, sizeof uboMirror);

        // Заглушка кадра: чистый цвет через glClear нет (no GL контекст смены) —
        // просто swap для проверки цикла/ввода.
        glfwSwapBuffers(win);
        if (++frame == maxFrames) break;
    }
    printf("step1 OK: %d frames, ubo=%zuB\n", frame, sizeof uboMirror);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
