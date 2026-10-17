// §14 F3c: ОБЩИЙ рантайм виджетов — один порядок слоёв, один рендер, один хит
// для ИГРЫ и для РЕДАКТОРА (self-host). Модель (ui_widget) остаётся чистой
// (без рендера): здесь единственная точка соприкосновения модель -> RcVecShape.
#ifndef ENGINE_UI_WIDGET_RUNTIME_H
#define ENGINE_UI_WIDGET_RUNTIME_H

#include "ui_widget.h"
#include "../render_api.h" // RcVecShape (примитив vec-ядра)

// Соседи узла parent в порядке слоёв (z, затем id) — единый порядок отрисовки,
// хита и дерева. Пишет id (не более UI_MAX_WIDGETS), возвращает число.
int ui_widget_sib_sorted(const UiWidgetTree *t, int parent, int *out);

// Отполнить всё дерево (корни->дети, по слоям) в sh, не более cap. Возвращает
// число примитивов. Прозрачные (bg[3]*alpha <= eps) пропускаются.
int ui_widget_build(const UiWidgetTree *t, int W, int H, RcVecShape *sh, int cap);

// Хит: ВЕРХНИЙ виджет под точкой (учёт z): последний по порядку слоёв = верх.
// 0 = мимо (нет виджета под точкой или дерево пусто).
int ui_widget_hit(const UiWidgetTree *t, int W, int H, float px, float py);

#endif // ENGINE_UI_WIDGET_RUNTIME_H
