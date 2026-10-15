// Атлас: baked RGBA? нет — R8 (одна компонента), белый глиф, цвет — uniform.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "third-party/stb/stb_truetype.h"
#include <glad/gl.h>
#include "rhi_gl.h"
#include "ui_text.h"

#define UI_ATLAS_W 256
#define UI_ATLAS_H 256
#define UI_GLYPHS 95 // 32..126

typedef struct {
    float x0, y0, x1, y1; // экран (y вниз),/to заполняет quad()
    float u0, v0, u1, v1;
    float rgba[4];
} UiQ;

struct UiText {
    unsigned tex;
    unsigned prog, vao, vbo;
    int uVP, uTex;
    stbtt_bakedchar ch[UI_GLYPHS];
    float px;
    UiQ q[UI_TEXT_MAXQ];
    int nq;
};

static unsigned text_prog(void) {
    // Шейдеры рядом с остальными (не инлайн-строки: hot-reload единообразен).
    return rhi_gl_prog("engine/shaders/ui/text.vert", "engine/shaders/ui/text.frag");
}

UiText *ui_text_init(const char *fontPath, float px) {
    FILE *ff = fopen(fontPath, "rb");
    if (!ff) {
        printf("ui_text: no font %s\n", fontPath);
        return 0;
    }
    fseek(ff, 0, SEEK_END);
    long sz = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    unsigned char *ttf = (unsigned char *)malloc((size_t)sz);
    if (!ttf) {
        fclose(ff);
        return 0;
    }
    if (fread(ttf, 1, (size_t)sz, ff) != (size_t)sz) {
        free(ttf);
        fclose(ff);
        return 0;
    }
    fclose(ff);
    unsigned char *bits = (unsigned char *)calloc((size_t)UI_ATLAS_W * UI_ATLAS_H, 1);
    if (!bits) {
        free(ttf);
        return 0;
    }
    UiText *t = (UiText *)calloc(1, sizeof *t);
    if (!t) {
        free(bits);
        free(ttf);
        return 0;
    }
    // Печём битмап напрямую (инит не нужен: BakeFontBitmap всё делает сам).
    int bakeRows = stbtt_BakeFontBitmap(ttf, 0, px, bits, UI_ATLAS_W, UI_ATLAS_H, 32, UI_GLYPHS, t->ch);
    printf("ui_text: bake rows=%d\n", bakeRows);
    free(ttf); // ttf больше не нужен (baked метрики скопированы)
    t->px = px;
    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, UI_ATLAS_W, UI_ATLAS_H, 0, GL_RED,
                 GL_UNSIGNED_BYTE, bits);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D, 0);
    free(bits);
    t->prog = text_prog();
    if (!t->prog) {
        glDeleteTextures(1, &t->tex);
        free(t);
        return 0;
    }
    glGenVertexArrays(1, &t->vao);
    glGenBuffers(1, &t->vbo);
    glBindVertexArray(t->vao);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)UI_TEXT_MAXQ * 4 * 8 * sizeof(float), 0,
                 GL_DYNAMIC_DRAW);
    // xyuv rgba(4): stride 8 float
    glVertexAttribPointer(0, 2, GL_FLOAT, 0, 8 * sizeof(float), (void *)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, 0, 8 * sizeof(float), (void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, 0, 8 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    t->uVP = glGetUniformLocation(t->prog, "uVP");
    t->uTex = glGetUniformLocation(t->prog, "uTex");
    // Отладка запекания: UI_ATLAS_DUMP=atlas.pgm (тестовая диагностика).
    {
        const char *dp = getenv("UI_ATLAS_DUMP");
        if (dp && dp[0]) {
            unsigned char *px = (unsigned char *)malloc((size_t)UI_ATLAS_W * UI_ATLAS_H);
            if (px) {
                glBindTexture(GL_TEXTURE_2D, t->tex);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, px);
                glBindTexture(GL_TEXTURE_2D, 0);
                FILE *ff = fopen(dp, "wb");
                if (ff) {
                    fprintf(ff, "P5\n%d %d\n255\n", UI_ATLAS_W, UI_ATLAS_H);
                    for (int y = UI_ATLAS_H - 1; y >= 0; y--)
                        fwrite(px + (size_t)y * UI_ATLAS_W, 1, UI_ATLAS_W, ff);
                    fclose(ff);
                    printf("ui_text: atlas dump %s\n", dp);
                }
                free(px);
            }
        }
    }
    printf("ui_text: atlas %dx%d px=%.0f OK\n", UI_ATLAS_W, UI_ATLAS_H, px);
    return t;
}

void ui_text_destroy(UiText *t) {
    if (!t) return;
    if (t->tex) glDeleteTextures(1, &t->tex);
    if (t->prog) glDeleteProgram(t->prog);
    if (t->vao) glDeleteVertexArrays(1, &t->vao);
    if (t->vbo) glDeleteBuffers(1, &t->vbo);
    free(t);
}

float ui_text_quad(UiText *t, float x, float y, const char *s, const float rgba[4]) {
    if (!t || !s) return 0.0f;
    float ox = x;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (t->nq >= UI_TEXT_MAXQ) break;
        if (*p < 32 || *p > 126) {
            ox += t->px * 0.5f;
            continue;
        }
        stbtt_bakedchar *b = &t->ch[*p - 32];
        float bw = b->x1 - b->x0, bh = b->y1 - b->y0;
        UiQ *q = &t->q[t->nq++];
        q->x0 = ox + b->xoff;
        q->y0 = y + b->yoff;
        q->x1 = q->x0 + bw;
        q->y1 = q->y0 + bh;
        q->u0 = b->x0 / (float)UI_ATLAS_W;
        q->u1 = b->x1 / (float)UI_ATLAS_W;
        // V БЕЗ флипа: строка 0 битмапа лежит на v=0 (первый байт -> t=0),
        // верх глифа (y0) семплирует v=y0/256. "Фикс" с 1-y инвертировал и
        // гнал семпл в пустые строки (красный тест это доказал: геометрия ок).
        q->v0 = b->y0 / (float)UI_ATLAS_H;
        q->v1 = b->y1 / (float)UI_ATLAS_H;
        memcpy(q->rgba, rgba, sizeof q->rgba);
        // Advance берём у stb (ox), квад уже заполнили выше сами.
        // (q обязателен non-NULL: внутри пишут поля даже когда нам не надо.)
        // fillrule=1 (opengl; 0=d3d9 даёт другое округление y — доки stb).
        stbtt_aligned_quad dummyq;
        float dummyY = y;
        stbtt_GetBakedQuad(t->ch, UI_ATLAS_W, UI_ATLAS_H, *p - 32, &ox, &dummyY, &dummyq, 1);
    }
    return ox - x;
}

void ui_text_flush(UiText *t, int w, int h) {
    if (!t || !t->nq || w <= 0 || h <= 0) {
        if (t) t->nq = 0;
        return;
    }
    // Вершины: 2 tri на квад (0,1,2 0,2,3), y вниз -> ortho [0,w]x[0,h].
    static float vb[(size_t)UI_TEXT_MAXQ * 6 * 8];
    int nv = 0;
    for (int i = 0; i < t->nq; i++) {
        UiQ *q = &t->q[i];
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        float px[4] = {q->x0, q->x1, q->x1, q->x0};
        float py[4] = {q->y0, q->y0, q->y1, q->y1};
        float pu[4] = {q->u0, q->u1, q->u1, q->u0};
        float pv[4] = {q->v0, q->v0, q->v1, q->v1};
        for (int k = 0; k < 6; k++) {
            int j = idx[k];
            vb[nv * 8 + 0] = px[j];
            vb[nv * 8 + 1] = py[j];
            vb[nv * 8 + 2] = pu[j];
            vb[nv * 8 + 3] = pv[j];
            memcpy(&vb[nv * 8 + 4], q->rgba, 4 * sizeof(float));
            nv++;
        }
    }
    float vp[16] = {
        2.0f / w, 0, 0, 0,
        0, -2.0f / h, 0, 0,
        0, 0, 1, 0,
        -1, 1, 0, 1,
    };
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(t->prog);
    glUniformMatrix4fv(t->uVP, 1, 0, vp);
    glUniform1i(t->uTex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glBindVertexArray(t->vao);
    glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)nv * 8 * sizeof(float), vb);
    glDrawArrays(GL_TRIANGLES, 0, nv);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    t->nq = 0;
}

unsigned ui_text_tex(const UiText *t) {
    return t ? t->tex : 0;
}
