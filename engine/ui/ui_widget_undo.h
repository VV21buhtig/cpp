// §14 F4: история (undo/redo) ДОКУМЕНТА виджетов — кольцо СНАПШОТОВ состояния.
// Почему снапшоты, а не объекты-команды: документ маленький (64 виджета ≈ 14 КБ),
// снапшот герметичен (не нужно писать/откатывать каждую операцию и бояться
// рассинхрона модели), и так делают Blender (global undo) и Photoshop (history
// states). Command-паттерн окупается, когда состояние большое (UE-транзакции) —
// это зафиксированный предел, а не недоделка: см. uitoolkit-composition.txt §16.
// Отдельно от ui_undo.h (тот — стек правок float-полей сущностей игры).
// Пул фиксированный: malloc нет.
#ifndef ENGINE_UI_UI_WIDGET_UNDO_H
#define ENGINE_UI_UI_WIDGET_UNDO_H

#include "ui_widget.h"

#define UI_WUNDO_RING 16
#define UI_WUNDO_LABEL 24

typedef struct {
    UiWidgetTree st[UI_WUNDO_RING]; // состояния истории (st[cur] = текущее)
    char what[UI_WUNDO_RING][UI_WUNDO_LABEL]; // метка шага, создавшего состояние
    int n;   // сколько состояний (0 = истории нет)
    int cur; // индекс текущего состояния (0..n-1)
} UiWidgetUndo;

void ui_wundo_init(UiWidgetUndo *u);

// Зафиксировать ТЕКУЩЕЕ состояние дерева как шаг истории. Вызывается НА КОММИТ
// операции (спавн/удаление/конец драга/конец правки поля), не на каждый шаг.
// Совпало с текущим состоянием — 0 (не плодим дубли). Новый шаг режет redo.
int ui_wundo_commit(UiWidgetUndo *u, const UiWidgetTree *t, const char *what);

// Откат/повтор: пишут состояние в *t (вызывающий обновляет рендер/файл).
int ui_wundo_undo(UiWidgetUndo *u, UiWidgetTree *t);
int ui_wundo_redo(UiWidgetUndo *u, UiWidgetTree *t);

int ui_wundo_depth(const UiWidgetUndo *u);      // шагов назад (0 = нечего)
int ui_wundo_redo_depth(const UiWidgetUndo *u); // шагов вперёд
const char *ui_wundo_label(const UiWidgetUndo *u, int dir); // -1 undo / +1 redo

#endif // ENGINE_UI_UI_WIDGET_UNDO_H
