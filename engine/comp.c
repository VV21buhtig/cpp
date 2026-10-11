// L3: порядок проходов, без GPU. C11, только render_api.h.
#include <stddef.h>
#include <string.h>
#include "comp.h"

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
        if (!passes[i].rc->frame_to(passes[i].rc, &v, t)) return ENG_COMP_BAD;
    }
    return ENG_COMP_OK;
}
