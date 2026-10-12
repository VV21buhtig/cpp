// Сцена: пул сущностей, C11. Без аллокаций после init (массивы внутри).
#include <string.h>
#include "scene.h"

static int idx(Entity e) { return (int)e - 1; }

void sc_init(ScWorld *w) {
    memset(w, 0, sizeof *w);
    for (int i = 0; i < SC_MAX; i++) {
        w->parent[i] = -1;
        w->visible[i] = 1;
        w->tr[i].scale = v3(1, 1, 1);
    }
}

Entity sc_spawn(ScWorld *w, const char *name) {
    if (!w) return 0;
    for (int i = 0; i < SC_MAX; i++) {
        if (w->alive[i]) continue;
        w->alive[i] = 1;
        w->parent[i] = -1;
        w->visible[i] = 1;
        w->tr[i].pos = v3(0, 0, 0);
        w->tr[i].rot = v3(0, 0, 0);
        w->tr[i].scale = v3(1, 1, 1);
        if (name) {
            strncpy(w->name[i], name, SC_NAME - 1);
            w->name[i][SC_NAME - 1] = 0;
        } else {
            w->name[i][0] = 0;
        }
        w->count++;
        return (Entity)(i + 1);
    }
    return 0;
}

void sc_kill(ScWorld *w, Entity e) {
    if (!w) return;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return;
    w->alive[i] = 0;
    w->count--;
    for (int k = 0; k < SC_MAX; k++)
        if (w->alive[k] && w->parent[k] == i) w->parent[k] = -1;
}

int sc_parent(ScWorld *w, Entity e, Entity parent) {    if (!w) return 0;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    if (!parent) { w->parent[i] = -1; return 1; }
    int p = idx(parent);
    if (p < 0 || p >= SC_MAX || !w->alive[p] || p == i) return 0;
    // Прямой цикл запрещаем (глубже — дело редактора, не ядра).
    if (w->parent[p] == i) return 0;
    w->parent[i] = p;
    return 1;
}

Entity sc_parent_of(const ScWorld *w, Entity e) {
    if (!w) return 0;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    int p = w->parent[i];
    if (p < 0 || p >= SC_MAX || !w->alive[p]) return 0;
    return (Entity)(p + 1);
}

ScTransform sc_get(const ScWorld *w, Entity e) {
    ScTransform t;
    memset(&t, 0, sizeof t);
    t.scale = v3(1, 1, 1);
    if (!w) return t;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return t;
    return w->tr[i];
}

void sc_set(ScWorld *w, Entity e, const ScTransform *t) {
    if (!w || !t) return;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return;
    w->tr[i] = *t;
}

int sc_visible(const ScWorld *w, Entity e) {
    if (!w) return 0;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return 0;
    return w->visible[i];
}

void sc_show(ScWorld *w, Entity e, int on) {
    if (!w) return;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return;
    w->visible[i] = on ? 1 : 0;
}

int sc_alive(const ScWorld *w, Entity e) {
    if (!w) return 0;
    int i = idx(e);
    return i >= 0 && i < SC_MAX && w->alive[i];
}

const char *sc_name(const ScWorld *w, Entity e) {
    if (!w) return "";
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return "";
    return w->name[i];
}

void sc_rename(ScWorld *w, Entity e, const char *name) {
    if (!w) return;
    int i = idx(e);
    if (i < 0 || i >= SC_MAX || !w->alive[i]) return;
    if (!name || !name[0]) return; // пустое не ставим
    strncpy(w->name[i], name, SC_NAME - 1);
    w->name[i][SC_NAME - 1] = 0;
}

int sc_list(const ScWorld *w, Entity *out, int cap) {
    if (!w || !out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < SC_MAX && n < cap; i++)
        if (w->alive[i]) out[n++] = (Entity)(i + 1);
    return n;
}
