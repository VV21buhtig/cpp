#ifndef ENGINE_GL_BLIT_H
#define ENGINE_GL_BLIT_H
// Блит CPU-кадра на экран поверх RHI-GL: текстура -> фулскрин -> present
// + бейдж + VOX_SHOT. Ядро 2D (vec/pix) сидит на этом, а не на сыром GL.
// static-функции: включает одно ядро.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhi_gl.h"

typedef struct {
    GLFWwindow *win;
    unsigned prog;
    unsigned vao;
    unsigned tex;
    int tw, th;
    int frame;
    double prevT;
    float fpsEma;
} BlitGL;

// Шейдеры блита ищутся от корня репо; запасной — рядом с движком.
static const char *blit_vs(void) {
    static const char *paths[] = {
        "engine/shaders/blit/blit.vert", "shaders/blit/blit.vert",
    };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) { fclose(f); return paths[i]; }
    }
    return paths[0];
}

static const char *blit_fs(void) {
    static const char *paths[] = {
        "engine/shaders/blit/blit.frag", "shaders/blit/blit.frag",
    };
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(paths[i], "rb");
        if (f) { fclose(f); return paths[i]; }
    }
    return paths[0];
}

static int blit_init(BlitGL *b, GLFWwindow *win) {
    memset(b, 0, sizeof *b);
    b->win = win;
    if (!rhi_gl_load(win)) return 0; // контекст + glad через RHI
    b->prog = rhi_gl_prog(blit_vs(), blit_fs());
    if (!b->prog) return 0;
    b->vao = rhi_gl_empty_vao();
    if (!b->vao) return 0;
    glGenTextures(1, &b->tex);
    b->prevT = glfwGetTime();
    return 1;
}

// Залить RGBA-кадр (низом вверх). Пересоздаёт текстуру при смене размера.
static int blit_upload(BlitGL *b, const uint8_t *rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return 0;
    glBindTexture(GL_TEXTURE_2D, b->tex);
    if (w != b->tw || h != b->th) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        b->tw = w;
        b->th = h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    return 1;
}

// Кадр: блит + бейдж (br,bg,bb) + VOX_SHOT. Возвращает fps.
static float blit_frame(BlitGL *b, int ww, int hh, float br, float bg, float bb) {
    double now = glfwGetTime();
    float dt = (float)(now - b->prevT);
    b->prevT = now;
    if (dt > 0.0f) {
        float fps = 1.0f / dt;
        b->fpsEma = b->fpsEma > 0.0f ? b->fpsEma * 0.95f + fps * 0.05f : fps;
    }
    glViewport(0, 0, ww, hh);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(b->prog);
    glBindTexture(GL_TEXTURE_2D, b->tex);
    glBindVertexArray(b->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 64, 32);
    glClearColor(br, bg, bb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    {
        const char *sp = getenv("VOX_SHOT");
        if (sp && sp[0]) {
            const char *sa = getenv("VOX_SHOT_AT");
            int at = sa && sa[0] ? atoi(sa) : 60;
            if (b->frame == at) {
                uint8_t *px = (uint8_t *)malloc((size_t)ww * hh * 3);
                if (px) {
                    glReadPixels(0, 0, ww, hh, GL_RGB, GL_UNSIGNED_BYTE, px);
                    FILE *f = fopen(sp, "wb");
                    if (f) {
                        fprintf(f, "P6\n%d %d\n255\n", ww, hh);
                        for (int y = hh - 1; y >= 0; y--)
                            fwrite(px + (size_t)y * ww * 3, 1, (size_t)ww * 3, f);
                        fclose(f);
                        fprintf(stderr, "shot: %s (%dx%d)\n", sp, ww, hh);
                    }
                    free(px);
                }
            }
        }
    }
    glfwSwapBuffers(b->win);
    b->frame++;
    return b->fpsEma;
}

static void blit_shutdown(BlitGL *b) {
    if (b->prog) glDeleteProgram(b->prog);
    if (b->vao) glDeleteVertexArrays(1, &b->vao);
    if (b->tex) glDeleteTextures(1, &b->tex);
}
#endif
