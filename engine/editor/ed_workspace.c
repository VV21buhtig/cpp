// ed_workspace: список режимов редактора (см. ed_workspace.h — зачем).
//
// Здесь ТОЛЬКО хранение и доступ. Никаких виджетов, доков и GL: режим заводит
// приложение, оболочка читает. Пустой id / переполнение — отказ (и скажет в stderr,
// как ed_cmd_add: молча терять данные нельзя).
#include "editor/ed_workspace.h"

#include <stdio.h>
#include <string.h>

static EdWorkspace g_ws[ED_WS_MAX];
static int g_wsN = 0;
static int g_wsActive = -1;

void ed_ws_reset(void) {
    memset(g_ws, 0, sizeof g_ws);
    g_wsN = 0;
    g_wsActive = -1;
}

int ed_ws_add(const EdWorkspace *w) {
    if (!w || !w->id || !w->id[0]) return -1;
    if (ed_ws_find(w->id) >= 0) return -1; // id уникален (иначе персист/тесты врут)
    if (g_wsN >= ED_WS_MAX) {
        fprintf(stderr, "ed_ws: режимов уже %d (предел %d) — \"%s\" не принят\n",
                g_wsN, ED_WS_MAX, w->id);
        return -1;
    }
    g_ws[g_wsN] = *w;
    if (g_ws[g_wsN].tabN > ED_TABS_MAX) g_ws[g_wsN].tabN = ED_TABS_MAX;
    if (g_ws[g_wsN].panelN > ED_PANEL_MAX) g_ws[g_wsN].panelN = ED_PANEL_MAX;
    if (g_wsActive < 0) g_wsActive = g_wsN; // первый заведённый становится активным
    return g_wsN++;
}

int ed_ws_n(void) { return g_wsN; }

const EdWorkspace *ed_ws_at(int i) {
    return (i >= 0 && i < g_wsN) ? &g_ws[i] : 0;
}

int ed_ws_find(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < g_wsN; i++)
        if (strcmp(g_ws[i].id, id) == 0) return i;
    return -1;
}

void ed_ws_active_set(int i) {
    if (i >= 0 && i < g_wsN) g_wsActive = i;
}

int ed_ws_active_i(void) { return g_wsActive; }

const EdWorkspace *ed_ws_active(void) { return ed_ws_at(g_wsActive); }

// --- что читает оболочка/режим (границы проверяются здесь ОДИН раз) ---

int ed_ws_tabs(void) {
    const EdWorkspace *w = ed_ws_active();
    return w ? w->tabN : 0;
}

const char *ed_ws_tab(int i) {
    const EdWorkspace *w = ed_ws_active();
    return (w && i >= 0 && i < w->tabN) ? w->tab[i] : "";
}

int ed_ws_panels(void) {
    const EdWorkspace *w = ed_ws_active();
    return w ? w->panelN : 0;
}

const EdPanelDesc *ed_ws_panel(int i) {
    const EdWorkspace *w = ed_ws_active();
    return (w && i >= 0 && i < w->panelN) ? &w->panel[i] : 0;
}

const char *ed_ws_caption(int i) {
    const EdPanelDesc *p = ed_ws_panel(i);
    return (p && p->caption) ? p->caption : "";
}

const char *ed_ws_tag(int i) {
    const EdPanelDesc *p = ed_ws_panel(i);
    return (p && p->tag) ? p->tag : "";
}

int ed_ws_dock(int i) {
    const EdPanelDesc *p = ed_ws_panel(i);
    return p ? (int)p->dock : ED_DK_LEFT;
}

int ed_ws_open_default(int i) {
    const EdPanelDesc *p = ed_ws_panel(i);
    return p ? (p->open != 0) : 0;
}

int ed_ws_look(int i) {
    const EdPanelDesc *p = ed_ws_panel(i);
    return p ? (int)p->look : 1;
}
