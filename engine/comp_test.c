// Тест композитора: порядок проходов + честные отказы. Без GPU.
#include <stdio.h>
#include <string.h>
#include "comp.h"

static int order[16];
static int norder = 0;

static int null_frame_to(RenderCore *rc, const RcView *v, RcTarget *t) {
    (void)v; (void)t;
    if (norder < 16) order[norder++] = (int)(rc->caps & 0xFF);
    return 1;
}

static RenderCore mkcore(unsigned caps, unsigned be, int hasTarget) {
    RenderCore rc;
    memset(&rc, 0, sizeof rc);
    rc.caps = caps;
    rc.backend = be;
    if (hasTarget) rc.frame_to = null_frame_to;
    return rc;
}

int main(void) {
    int bad = 0;
    RcTarget *t = (RcTarget *)0x1234; // заглушка цели
    // 1. Порядок 3 проходов x2 кадра.
    {
        RenderCore a = mkcore(1, RC_BACKEND_GL, 1);
        RenderCore b = mkcore(2, RC_BACKEND_GL, 1);
        RenderCore c = mkcore(4, RC_BACKEND_GL, 1);
        EngPass ps[3] = {{&a, 0, 0, 0}, {&b, 0, 0, 0}, {&c, 0, 0, 0}};
        norder = 0;
        if (eng_comp_frame(ps, 3, t) != ENG_COMP_OK) { printf("COMP FAIL: ok\n"); bad = 1; }
        if (eng_comp_frame(ps, 3, t) != ENG_COMP_OK) { printf("COMP FAIL: ok2\n"); bad = 1; }
        int want[6] = {1, 2, 4, 1, 2, 4};
        if (norder != 6 || memcmp(order, want, sizeof want)) {
            printf("COMP FAIL: order (got %d)\n", norder);
            bad = 1;
        }
    }
    // 2. Смешанные бэкенды — отказ.
    {
        RenderCore a = mkcore(1, RC_BACKEND_GL, 1);
        RenderCore b = mkcore(1, RC_BACKEND_VK, 1);
        EngPass ps[2] = {{&a, 0, 0, 0}, {&b, 0, 0, 0}};
        if (eng_comp_frame(ps, 2, t) != ENG_COMP_MIXED) { printf("COMP FAIL: mixed\n"); bad = 1; }
    }
    // 3. Проход без frame_to — отказ.
    {
        RenderCore a = mkcore(1, RC_BACKEND_GL, 1);
        RenderCore b = mkcore(1, RC_BACKEND_GL, 0);
        EngPass ps[2] = {{&a, 0, 0, 0}, {&b, 0, 0, 0}};
        if (eng_comp_frame(ps, 2, t) != ENG_COMP_NO_TARGET) {
            printf("COMP FAIL: notarget\n");
            bad = 1;
        }
    }
    // 4. Пусто — отказ.
    if (eng_comp_frame(0, 0, t) != ENG_COMP_BAD) { printf("COMP FAIL: bad\n"); bad = 1; }
    printf(bad ? "COMP FAIL\n" : "COMP OK\n");
    return bad;
}
