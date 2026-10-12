// L3: порядок проходов, без GPU. C11, только render_api.h + вид цели.
#include <stddef.h>
#include <string.h>
#include "comp.h"
#include "rtarget.h"

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
        // Первый проход рисует с чистками и небом, остальные — в глубину
        // (noClear): небо поздних не закрашивает ранние, тест глубины общий.
        t->noClear = (i > 0);
        if (!passes[i].rc->frame_to(passes[i].rc, &v, t)) return ENG_COMP_BAD;
    }
    t->noClear = 0; // цель отдаём чистой (следующий кадр снова с первого)
    return ENG_COMP_OK;
}
