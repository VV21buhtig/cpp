// Песочница Noita-like: клеточный автомат частиц + pix-ядро.
// L4-частицы: гравитация и движение — правила одной частицы
// (песок падает/осыпается, вода течёт, огонь всплывает и гаснет).
// Сетка 320x180 клеток -> апскейл NEAREST в окно (крупный пиксель как в Noita).
// Мышь: ЛКМ песок, ПКМ вода, СКМ камень, C очистить. --frames N, VOX_SHOT.
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "render_api.h"

RenderCore *rc_pix_create(void);

#define SW 320
#define SH 180

enum { M_AIR = 0, M_SAND, M_WATER, M_STONE, M_FIRE };

static uint8_t grid[SW * SH];
static uint8_t fireLife[SW * SH];
static int simFrame = 0;

static inline uint8_t at(int x, int y) {
    if (x < 0 || y < 0 || x >= SW || y >= SH) return M_STONE; // стены держат
    return grid[(size_t)y * SW + x];
}

static inline void setm(int x, int y, uint8_t m, uint8_t life) {
    if (x < 0 || y < 0 || x >= SW || y >= SH) return;
    grid[(size_t)y * SW + x] = m;
    fireLife[(size_t)y * SW + x] = life;
}

// Одна частица: x,y. Возвращает 1 если двинулась (больше не трогаем в кадре).
static int step_particle(int x, int y) {
    uint8_t m = at(x, y);
    if (m == M_AIR || m == M_STONE) return 0;
    int dir = (simFrame + x + y) & 1 ? 1 : -1; // шахматный порядок без bias
    if (m == M_SAND) {
        // Вниз, своп с водой/огнём (тонем), иначе осыпь по диагонали.
        uint8_t b = at(x, y + 1);
        if (b == M_AIR || b == M_WATER || b == M_FIRE) {
            setm(x, y, b, 0);
            setm(x, y + 1, M_SAND, 0);
            return 1;
        }
        for (int k = 0; k < 2; k++) {
            int nx = x + (k ? -dir : dir);
            uint8_t d = at(nx, y + 1);
            if (d == M_AIR || d == M_WATER || d == M_FIRE) {
                setm(x, y, d, 0);
                setm(nx, y + 1, M_SAND, 0);
                return 1;
            }
        }
        return 0;
    }
    if (m == M_WATER) {
        uint8_t b = at(x, y + 1);
        if (b == M_AIR || b == M_FIRE) {
            setm(x, y, b, 0);
            setm(x, y + 1, M_WATER, 0);
            return 1;
        }
        // Течём вбок (и по диагонали вниз), тонем в песок свопом.
        for (int k = 0; k < 2; k++) {
            int nx = x + (k ? -dir : dir);
            uint8_t d = at(nx, y + 1);
            uint8_t s = at(nx, y);
            if (d == M_AIR || d == M_FIRE) {
                setm(x, y, d, 0);
                setm(nx, y + 1, M_WATER, 0);
                return 1;
            }
            if (s == M_AIR || s == M_FIRE) {
                setm(x, y, s, 0);
                setm(nx, y, M_WATER, 0);
                return 1;
            }
        }
        return 0;
    }
    // Огонь: всплывает, гаснет, воду не трогает.
    uint8_t *life = &fireLife[(size_t)y * SW + x];
    if (*life > 0) (*life)--;
    else { setm(x, y, M_AIR, 0); return 1; }
    if (at(x, y - 1) == M_AIR) {
        uint8_t l = *life;
        setm(x, y, M_AIR, 0);
        setm(x, y - 1, M_FIRE, l);
        return 1;
    }
    int nx = x + dir;
    if (at(nx, y - 1) == M_AIR) {
        uint8_t l = *life;
        setm(x, y, M_AIR, 0);
        setm(nx, y - 1, M_FIRE, l);
        return 1;
    }
    return 0;
}

static void sim_step(void) {
    // Снизу вверх: нижние уже легли, верхние падают на них.
    for (int y = SH - 1; y >= 0; y--)
        for (int x = 0; x < SW; x++) step_particle(x, y);
    simFrame++;
}

static void brush(int cx, int cy, uint8_t m, int r) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) setm(cx + x, cy + y, m, m == M_FIRE ? 40 : 0);
}

int main(int argc, char **argv) {
    int maxFrames = 0;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) maxFrames = atoi(argv[++i]);
    if (!glfwInit()) { fprintf(stderr, "glfwInit fail\n"); return 1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *win = glfwCreateWindow(640, 360, "sand demo", 0, 0);
    if (!win) { fprintf(stderr, "window fail\n"); glfwTerminate(); return 1; }
    RenderCore *rc = rc_pix_create();
    if (!rc || !rc->init(rc, win)) { fprintf(stderr, "pix init fail\n"); return 1; }
    printf("core: %s caps=0x%x be=%u\n", rc->name, rc->caps, rc->backend);
    static uint8_t px[SW * SH * 4];
    int frame = 0;
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        if (maxFrames && frame >= maxFrames) break;
        if (glfwGetKey(win, GLFW_KEY_C) == GLFW_PRESS) {
            memset(grid, 0, sizeof grid);
            memset(fireLife, 0, sizeof fireLife);
        }
        // Эмиттеры для детерминированного прогона: песок сверху, вода с боков.
        brush(SW / 2, 2, M_SAND, 4);
        if (frame % 3 == 0) {
            brush(10, 10, M_WATER, 3);
            brush(SW - 10, 10, M_WATER, 3);
        }
        if (frame % 7 == 0) brush(SW / 2 + 30, SH - 20, M_FIRE, 2);
        // Кисть мышью.
        double mx, my;
        glfwGetCursorPos(win, &mx, &my);
        int gx = (int)(mx / 640.0 * SW), gy = (int)(my / 360.0 * SH);
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
            brush(gx, gy, M_SAND, 4);
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            brush(gx, gy, M_WATER, 4);
        if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            brush(gx, gy, M_STONE, 4);
        sim_step();
        // Сетка -> RGBA (строка 0 = верх).
        for (int i = 0; i < SW * SH; i++) {
            uint8_t *p = px + (size_t)i * 4;
            switch (grid[i]) {
            case M_SAND: p[0] = 220; p[1] = 190; p[2] = 60; break;
            case M_WATER: p[0] = 40; p[1] = 110; p[2] = 220; break;
            case M_STONE: p[0] = 130; p[1] = 130; p[2] = 135; break;
            case M_FIRE: p[0] = 255; p[1] = 110; p[2] = 20; break;
            default: p[0] = 8; p[1] = 8; p[2] = 12; break;
            }
            p[3] = 255;
        }
        if (!rc->upload_pixels(rc, px, SW, SH)) { fprintf(stderr, "upload fail\n"); return 1; }
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
