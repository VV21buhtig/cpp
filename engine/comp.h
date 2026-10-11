#ifndef ENGINE_COMP_H
#define ENGINE_COMP_H
// L3: композиция доменов. Проходы одного бэкенда в общую цель по порядку:
// view -> feed -> frame_to на каждый проход. GPU-композиция требует
// frame_to у всех проходов (иначе ENG_COMP_NO_TARGET, честный отказ).
// Очистка/бленд между проходами — дело цели и порядка, не ядер.
#include "render_api.h"

#define ENG_COMP_OK 0
#define ENG_COMP_BAD -1        // пустые проходы/ядра
#define ENG_COMP_MIXED -2      // разные бэкенды в одном кадре (нельзя)
#define ENG_COMP_NO_TARGET -3  // проход без frame_to (только свой экран)

typedef struct {
    RenderCore *rc;
    void *ctx;
    void (*view)(void *ctx, RcView *v);
    void (*feed)(void *ctx, RenderCore *rc);
} EngPass;

// Прогнать проходы по порядку в цель t. Возвращает ENG_COMP_*.
int eng_comp_frame(const EngPass *passes, int n, RcTarget *t);
#endif
