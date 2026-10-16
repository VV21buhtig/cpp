// L3: порядок проходов, без GPU. C11, только render_api.h + вид цели.
#include <stddef.h>
#include <string.h>
#include "comp.h"
#include "rtarget.h"

void eng_pass_begin(RcTarget *t, float r, float g, float b, float a, float depth, int doClear) {
    if (!t) return;
    t->passClear = doClear ? 1 : 0;
    t->clearR = r;
    t->clearG = g;
    t->clearB = b;
    t->clearA = a;
    t->clearDepth = depth;
}

void eng_pass_end(RcTarget *t) {
    if (!t) return;
    t->passClear = 0;
}

int eng_comp_frame(const EngPass *passes, int n, RcTarget *t) {
    if (!passes || n <= 0) return ENG_COMP_BAD;
    for (int i = 0; i < n; i++)
        if (!passes[i].rc) return ENG_COMP_BAD;
    unsigned be = passes[0].rc->backend;
    for (int i = 0; i < n; i++) {
        if (passes[i].rc->backend != be) return ENG_COMP_MIXED;
        if (!passes[i].rc->frame_to) return ENG_COMP_NO_TARGET;
    }
    for (int i = 0; i < n; i++) {
        RcView v;
        // Нулевой вид по умолчанию (размер цели ядро узнает из t).
        // Видовые поля сверх — через view().
        memset(&v, 0, sizeof v);
        if (passes[i].view) passes[i].view(passes[i].ctx, &v);
        if (passes[i].feed) passes[i].feed(passes[i].ctx, passes[i].rc);
        // Первый проход чистит и рисует небо, остальные — в глубину:
        // небо поздних не закрашивает ранние, тест глубины общий.
        // Чистка — решение композитора через begin (ядра не решают сами).
        eng_pass_begin(t, 0.05f, 0.07f, 0.12f, 1.0f, 1.0f, i == 0);
        if (!passes[i].rc->frame_to(passes[i].rc, &v, t)) return ENG_COMP_BAD;
        eng_pass_end(t);
    }
    return ENG_COMP_OK;
}
