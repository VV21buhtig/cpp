#ifndef ENGINE_COMP_GL_H
#define ENGINE_COMP_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// GL-цель композитора: FBO + RGBA-текстура + показ на экран.
// Правило проходов v1: проход 0 (база, воксели) чистит и рисует,
// остальные (UI) — рисуют поверх БЕЗ чистки, с блендом.
// static-функции: включает композитное демо.
#include <stdio.h>
#include <stdlib.h>
#include "rhi_gl.h"
#include "rtarget.h"
#include "render_api.h" // typedef RcTarget
#include "rtarget.h"
#include "render_api.h" // typedef RcTarget

static int comp_target_gl_new(RcTarget *t, int w, int h) {
    if (!t || w <= 0 || h <= 0) return 0;
    t->backend = 2; // RC_BACKEND_GL (без render_api.h ради лёгкости)
    t->w = w;
    t->h = h;
    t->fbo = 0;
    t->tex = 0;
    t->dbo = 0;
    glGenFramebuffers(1, &t->fbo);
    glGenTextures(1, &t->tex);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenRenderbuffers(1, &t->dbo);
    glBindRenderbuffer(GL_RENDERBUFFER, t->dbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->dbo);
    int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        fprintf(stderr, "comp: FBO incomplete\n");
        glDeleteFramebuffers(1, &t->fbo);
        glDeleteTextures(1, &t->tex);
        glDeleteRenderbuffers(1, &t->dbo);
        t->fbo = t->tex = t->dbo = 0;
        return 0;
    }
    return 1;
}

static void comp_target_gl_free(RcTarget *t) {
    if (!t) return;
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->tex) glDeleteTextures(1, &t->tex);
    if (t->dbo) glDeleteRenderbuffers(1, &t->dbo);
    t->fbo = t->tex = t->dbo = 0;
}

// Показать цель на экран: блит её текстуры фулскрином.
// prog/vao — программа блита вызывающего (rhi_gl_prog + empty_vao снаружи).
static void comp_present_gl(unsigned prog, unsigned vao, const RcTarget *t, int ww, int hh) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ww, hh);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(prog);
    glBindTexture(GL_TEXTURE_2D, t->tex);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
}

#ifdef __cplusplus
}
#endif
#endif // ENGINE_COMP_GL_H
