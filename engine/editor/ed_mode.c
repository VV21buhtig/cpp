// ed_mode: таблица режимов редактора — одна реализация на всех (см. ed_mode.h).
#include "editor/ed_mode.h"

#include <string.h>

static const EdMode *g_mode[ED_MODE_MAX]; // записи-указатели: таблицы живут у режимов
static int g_n;

void ed_mode_add(const EdMode *m) {
    if (!m || !m->id || g_n >= ED_MODE_MAX) return;
    if (ed_mode_find(m->id)) return; // один id дважды — не заводим (тихо: это ошибка входа)
    g_mode[g_n++] = m;
}

int ed_mode_n(void) { return g_n; }

const EdMode *ed_mode_at(int i) { return (i >= 0 && i < g_n) ? g_mode[i] : 0; }

const EdMode *ed_mode_find(const char *id) {
    if (!id) return 0;
    for (int i = 0; i < g_n; i++)
        if (strcmp(g_mode[i]->id, id) == 0) return g_mode[i];
    return 0;
}
