#ifndef CORE_GL_BLIT_H
#define CORE_GL_BLIT_H
// Общий GL-блит для 2D-ядер (pixel/vector): CPU-кадр RGBA (низом вверх,
// как GL-текстура) -> текстура -> фулскрин -> present + бейдж + VOX_SHOT.
// static-функции: включают по одному разу в pix_core.c и vec_core.c.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLIT_SHADER_DIR "shaders/blit"

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

static char *blit_read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "blit: no file %s\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) { fclose(f); return 0; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return 0; }
    s[n] = 0;
    fclose(f);
    return s;
}

// Байт бейджа задаётся цветом в blit_frame.
static int blit_init(BlitGL *b, GLFWwindow *win) {
    memset(b, 0, sizeof *b);
    b->win = win;
    glfwMakeContextCurrent(win);
    if (!gladLoadGL(glfwGetProcAddress)) { fprintf(stderr, "blit: glad fail\n"); return 0; }
    if (!GLAD_GL_VERSION_4_5) { fprintf(stderr, "blit: need 4.5\n"); return 0; }
    char *vsc = blit_read(BLIT_SHADER_DIR "/blit.vert");
    char *fsc = blit_read(BLIT_SHADER_DIR "/blit.frag");
    if (!vsc || !fsc) { free(vsc); free(fsc); return 0; }
    unsigned v = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(v, 1, (const char **)&vsc, 0);
    glCompileShader(v);
    unsigned f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(f, 1, (const char **)&fsc, 0);
    glCompileShader(f);
    b->prog = glCreateProgram();
    glAttachShader(b->prog, v);
    glAttachShader(b->prog, f);
    glLinkProgram(b->prog);
    int ok = 0;
    glGetProgramiv(b->prog, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    free(vsc);
    free(fsc);
    if (!ok) { fprintf(stderr, "blit: link fail\n"); return 0; }
    glGenVertexArrays(1, &b->vao);
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
