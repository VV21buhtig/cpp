#ifndef ENGINE_COMP_GL_H
#define ENGINE_COMP_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// GL-цель композитора: показ на экран (present). Создание/смерть целей —
// единый API eng_target_* (comp_target.c, P1 тёплого ABI); статиков тут нет.
#include <stdio.h>
#include <stdlib.h>
#include "rhi_gl.h"
#include "rtarget.h"
#include "render_api.h" // typedef RcTarget

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
