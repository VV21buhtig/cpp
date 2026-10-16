#ifndef ENGINE_COMP_H
#define ENGINE_COMP_H

#ifdef __cplusplus
extern "C" {
#endif

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

// W2: явный проход. begin — ЕДИНСТВЕННЫЙ писатель passClear/цвета
// (чисто состояние, без GPU — comp_test остаётся безгловым).
// doClear=1: проход чистит цель этим цветом/глубиной (первый проход, одиночки).
void eng_pass_begin(RcTarget *t, float r, float g, float b, float a, float depth, int doClear);
// Проход закрыт: чистка потреблена (следующий кадр снова с begin).
void eng_pass_end(RcTarget *t);

// W1: lifecycle целей (P1 тёплого ABI). GL — настоящие; VK/WebGPU — 0
// (целями владеют ядра, см. rtarget.h nobj; доведут в W3/P3).
int eng_target_new(unsigned backend, int w, int h, RcTarget *t);
void eng_target_free(RcTarget *t);
// Пересоздать под размер на месте (тот же размер = no-op 1).
int eng_target_resize(RcTarget *t, int w, int h);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_COMP_H
