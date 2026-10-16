// W1: lifecycle целей композитора (P1 тёплого ABI).
// GL-реализация (FBO+RGBA+depth); VK/WebGPU-целями владеют ядра
// (см. rtarget.h nobj) — для них eng_target_* возвращают 0, это честный
// отказ, а не дыра (W3/P3 доведут их следом).
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "comp.h"
#include "rtarget.h"
#include "rhi_gl.h"

int eng_target_new(unsigned backend, int w, int h, RcTarget *t) {
    if (!t || w <= 0 || h <= 0) return 0;
    memset(t, 0, sizeof *t);
    if (backend != 2) return 0; // только GL; VK/WG — цели ядер (см. выше)
    t->backend = 2;
    t->layout = RC_LAYOUT_UNDEFINED; // содержимого ещё нет
    t->w = w;
    t->h = h;
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
        fprintf(stderr, "eng_target: FBO incomplete\n");
        eng_target_free(t);
        return 0;
    }
    return 1;
}

void eng_target_free(RcTarget *t) {
    if (!t) return;
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->tex) glDeleteTextures(1, &t->tex);
    if (t->dbo) glDeleteRenderbuffers(1, &t->dbo);
    t->fbo = t->tex = t->dbo = 0;
}

int eng_target_resize(RcTarget *t, int w, int h) {
    if (!t || w <= 0 || h <= 0) return 0;
    if (t->backend != 0 && t->backend != 2) return 0; // VK/WG — цели ядер
    if (t->backend == 2 && t->w == w && t->h == h && t->fbo) return 1; // no-op
    eng_target_free(t);
    memset(t, 0, sizeof *t);
    return eng_target_new(2, w, h, t);
}
