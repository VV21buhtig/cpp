#ifndef ENGINE_GL_BLIT_H
#define ENGINE_GL_BLIT_H

#ifdef __cplusplus
extern "C" {
#endif

// Блит CPU-кадра на экран поверх RHI-GL: текстура -> фулскрин -> present
// + бейдж + VOX_SHOT. Ядро 2D (vec/pix) сидит на этом, а не на сыром GL.
// static-функции: включает одно ядро.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rhi_gl.h"

typedef struct {
    GLFWwindow *win;
    RhiGlProg prog;
    unsigned vao;
    unsigned tex;
    int tw, th;
    int frame;
    double prevT;
    float fpsEma;
} BlitGL;

// Пути шейдеров — через VFS (vfs_resolve в prog_load).
static int blit_init(BlitGL *b, GLFWwindow *win) {
    memset(b, 0, sizeof *b);
    b->win = win;
    if (!rhi_gl_load(win)) return 0; // контекст + glad через RHI
    if (!rhi_gl_prog_load(&b->prog, "shaders/blit/blit.vert", "shaders/blit/blit.frag"))
        return 0;
    b->vao = rhi_gl_empty_vao();
    if (!b->vao) return 0;
    glGenTextures(1, &b->tex);
    b->prevT = glfwGetTime();
    return 1;
}

// Горячая замена шейдера блита (Monaco): swap, refetch нечего (юниформ один
// сэмплер, биндится каждый draw). При отказе старая жива.
static int blit_reload(BlitGL *b, const char *id, const char *code, char *log, int logcap) {
    char lmem[2048] = {0};
    unsigned np = rhi_gl_prog_reload(&b->prog, id, code, lmem, sizeof lmem);
    if (!np) {
        if (log && logcap > 0) {
            strncpy(log, lmem, (size_t)logcap - 1);
            log[logcap - 1] = 0;
        }
        return 0;
    }
    glDeleteProgram(b->prog.prog);
    b->prog.prog = np;
    if (log && logcap > 0) snprintf(log, (size_t)logcap, "blit: %s", lmem);
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

// Частичная заливка (dirty-rect, инвалидация по-UE): регион (x,y,w,h)
// в ТЕКСТУРНЫХ координатах (y вверх, как GL), данные — строки снизу вверх.
// Текстура должна существовать (размер не меняем).
static int blit_upload_rect(BlitGL *b, const uint8_t *rgba, int x, int y, int w, int h) {
    if (!rgba || !b || !b->tex || w <= 0 || h <= 0) return 0;
    glBindTexture(GL_TEXTURE_2D, b->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return 1;
}

// Кадр в чужой FBO поверх (бленд src-alpha): fb уже залит в текстуру блита.
// Без чистки (её сделал базовый проход), без swap/бейджа.
static void blit_overlay_to(BlitGL *b, unsigned fbo, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(b->prog.prog);
    glActiveTexture(GL_TEXTURE0); // юнит сэмплера (воксель оставляет 1!)
    glBindTexture(GL_TEXTURE_2D, b->tex);
    glBindVertexArray(b->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
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
    glUseProgram(b->prog.prog);
    glActiveTexture(GL_TEXTURE0);
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
    if (b->prog.prog) glDeleteProgram(b->prog.prog);
    if (b->vao) glDeleteVertexArrays(1, &b->vao);
    if (b->tex) glDeleteTextures(1, &b->tex);
}

#ifdef __cplusplus
}
#endif
#endif // ENGINE_GL_BLIT_H
