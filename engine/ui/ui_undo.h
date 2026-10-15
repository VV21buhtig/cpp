// Стек undo/redo для float-полей сущности (X-дескрипторыUiField).
// Запись: {entity, fieldIndex, before, after}. Фикс.кап, malloc нет вообще
// (массив внутри). Redo-стек чистится любым новым push (как везде).
#ifndef ENGINE_UI_UNDO_H
#define ENGINE_UI_UNDO_H

#define UI_UNDO_CAP 64

typedef struct {
    unsigned ent;
    int field;
    float before, after;
} UiCmd;

typedef struct {
    UiCmd undo[UI_UNDO_CAP];
    int nundo;
    UiCmd redo[UI_UNDO_CAP];
    int nredo;
} UiUndo;

void ui_undo_reset(UiUndo *u);
// Коммит mouse-up: запомнить {было->стало}. 0 если игнор (то же значение).
int ui_undo_push(UiUndo *u, unsigned ent, int field, float before, float after);
// Откат/повтор: пишут результат в *out (применить — дело вызывающего через
// ui_field_set_f + sc_set). Возвращают 1 если было что откатить.
int ui_undo_undo(UiUndo *u, UiCmd *out);
int ui_undo_redo(UiUndo *u, UiCmd *out);

#endif // ENGINE_UI_UNDO_H
