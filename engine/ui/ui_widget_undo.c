// §14 F4: кольцо истории документа виджетов (см. ui_widget_undo.h — почему
// снапшоты). Линейная история с курсором: st[0..n-1], st[cur] = текущее
// состояние. Кольцо полное — старейшее вываливается сдвигом (история конечна
// — честно, как ui_undo.c у полей: «undone по кругу» не притворяемся).
#include <string.h>
#include <stdio.h>
#include "ui_widget_undo.h"

void ui_wundo_init(UiWidgetUndo *u) {
    if (!u) return;
    memset(u, 0, sizeof *u);
}

int ui_wundo_commit(UiWidgetUndo *u, const UiWidgetTree *t, const char *what) {
    if (!u || !t) return 0;
    // То же состояние — шага не делаем (иначе история забивается пустышками:
    // клик без движения, повторный UP и т.п.).
    if (u->n > 0 && memcmp(&u->st[u->cur], t, sizeof *t) == 0) return 0;
    if (u->n > 0 && u->cur + 1 < u->n) u->n = u->cur + 1; // новая ветка режет redo
    if (u->n == UI_WUNDO_RING) {
        memmove(&u->st[0], &u->st[1], (size_t)(UI_WUNDO_RING - 1) * sizeof u->st[0]);
        memmove(&u->what[0], &u->what[1],
                (size_t)(UI_WUNDO_RING - 1) * (size_t)UI_WUNDO_LABEL);
        u->n--;
    }
    u->st[u->n] = *t; // копия структуры (внутри только массивы; malloc нет)
    snprintf(u->what[u->n], UI_WUNDO_LABEL, "%s", what && what[0] ? what : "edit");
    u->n++;
    u->cur = u->n - 1;
    return 1;
}

int ui_wundo_undo(UiWidgetUndo *u, UiWidgetTree *t) {
    if (!u || !t || u->cur <= 0) return 0;
    u->cur--;
    *t = u->st[u->cur];
    return 1;
}

int ui_wundo_redo(UiWidgetUndo *u, UiWidgetTree *t) {
    if (!u || !t || u->cur + 1 >= u->n) return 0;
    u->cur++;
    *t = u->st[u->cur];
    return 1;
}

int ui_wundo_depth(const UiWidgetUndo *u) {
    return u && u->n > 0 ? u->cur : 0;
}

int ui_wundo_redo_depth(const UiWidgetUndo *u) {
    return u && u->n > 0 ? u->n - 1 - u->cur : 0;
}

const char *ui_wundo_label(const UiWidgetUndo *u, int dir) {
    if (!u || u->n <= 0) return "";
    int i = dir > 0 ? u->cur + 1 : u->cur;
    if (i < 0 || i >= u->n) return "";
    return u->what[i];
}
