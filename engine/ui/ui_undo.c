// История правок: два стека фиксированного размера, ноль аллокаций.
// Переполнение: старое вываливается сдвигом (история конечна — честно).
#include <string.h>
#include "ui_undo.h"

void ui_undo_reset(UiUndo *u) {
    if (!u) return;
    memset(u, 0, sizeof *u);
}

int ui_undo_push(UiUndo *u, unsigned ent, int field, float before, float after) {
    if (!u || !ent || field < 0 || before == after) return 0;
    if (u->nundo >= UI_UNDO_CAP)
        memmove(&u->undo[0], &u->undo[1], (size_t)(UI_UNDO_CAP - 1) * sizeof u->undo[0]);
    else
        u->nundo++;
    u->undo[u->nundo - 1].ent = ent;
    u->undo[u->nundo - 1].field = field;
    u->undo[u->nundo - 1].before = before;
    u->undo[u->nundo - 1].after = after;
    u->nredo = 0; // новая правка убивает redo (как везде)
    return 1;
}

int ui_undo_undo(UiUndo *u, UiCmd *out) {
    if (!u || !out || !u->nundo) return 0;
    *out = u->undo[--u->nundo];
    if (u->nredo < UI_UNDO_CAP) u->redo[u->nredo++] = *out;
    return 1;
}

int ui_undo_redo(UiUndo *u, UiCmd *out) {
    if (!u || !out || !u->nredo) return 0;
    *out = u->redo[--u->nredo];
    if (u->nundo < UI_UNDO_CAP) u->undo[u->nundo++] = *out;
    return 1;
}
