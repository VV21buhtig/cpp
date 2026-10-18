// ed_cmd: реализация командного ядра редактора. См. ed_cmd.h — здесь НЕТ ни
// виджетов, ни сцены: только реестр, клавиши и вызов через сервисы EdCmdHost.
#include "editor/ed_cmd.h"

#include <stdio.h>
#include <string.h>

static EdCmd g_cmd[ED_CMD_MAX];
static int g_cmdN = 0;
static const EdCmdHost *g_host = 0;

void ed_cmd_reset(void) {
    g_cmdN = 0;
    g_host = 0;
}

void ed_cmd_host(const EdCmdHost *h) { g_host = h; }

int ed_cmd_add(const EdCmd *c) {
    if (!c || !c->id || !c->id[0]) return -1; // без id запись неадресуема
    if (g_cmdN >= ED_CMD_MAX) return -1;      // молча не теряем: вызывающий видит -1
    for (int i = 0; i < g_cmdN; i++)
        if (strcmp(g_cmd[i].id, c->id) == 0) return -1; // дубль id — отказ
    g_cmd[g_cmdN] = *c;
    return g_cmdN++;
}

int ed_cmd_n(void) { return g_cmdN; }

const EdCmd *ed_cmd_at(int i) { return (i >= 0 && i < g_cmdN) ? &g_cmd[i] : 0; }

const EdCmd *ed_cmd_all(void) { return g_cmd; }

int ed_cmd_find(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < g_cmdN; i++)
        if (strcmp(g_cmd[i].id, id) == 0) return i;
    return -1;
}

// Метка записи по её виду и доводу: «как называется тип, который создаёт эта
// запись». Нужна, чтобы имена типов НЕ дублировались третьей копией строк.
const char *ed_cmd_label_of(int kind, int arg) {
    for (int i = 0; i < g_cmdN; i++)
        if (g_cmd[i].kind == kind && g_cmd[i].arg == arg) return g_cmd[i].label;
    return "?";
}

// ОДНА КНОПКА НА ДЕЙСТВИЕ: у записи есть свой вид (палитра/шапка панели) —
// значит в ленте она не рисуется. Клавиша и строка шпаргалки остаются.
int ed_cmd_ribbon(int i) {
    const EdCmd *a = ed_cmd_at(i);
    return a && a->views == 0;
}

int ed_cmd_enabled(int i) {
    if (!ed_cmd_at(i)) return 0;
    return g_host && g_host->enabled ? g_host->enabled(i) : 1;
}

int ed_cmd_on(int i) {
    if (!ed_cmd_at(i)) return 0;
    return g_host && g_host->on ? g_host->on(i) : 0;
}

// Гейт ЗДЕСЬ и только здесь: вид (лента/палитра/клавиша) не решает сам,
// доступно ли действие, — иначе «серая кнопка сработала» на одном пути из трёх.
int ed_cmd_run(int i) {
    if (!ed_cmd_enabled(i)) return 0;
    return g_host && g_host->run ? g_host->run(i) : 0;
}

// Клавиша -> запись. Ctrl/Shift значимы: Ctrl+Z != Ctrl+Shift+Z.
int ed_cmd_by_key(int mods, int key) {
    int m = mods & ED_MOD_MASK;
    for (int i = 0; i < g_cmdN; i++) {
        const EdCmd *a = &g_cmd[i];
        if (a->key && a->key == key && (a->mods & ED_MOD_MASK) == m) return i;
        if (a->key2 && a->key2 == key && (a->mods2 & ED_MOD_MASK) == m) return i;
    }
    return -1;
}

// Имя клавиши с модификатором: "Ctrl+Z", "Del", "F2".
const char *ed_cmd_key_name(int key, int mods, char *buf, int cap) {
    char k[16];
    if (key == 0) k[0] = 0;
    else if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        k[0] = (char)('A' + (key - GLFW_KEY_A));
        k[1] = 0;
    } else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        k[0] = (char)('0' + (key - GLFW_KEY_0));
        k[1] = 0;
    } else if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
        snprintf(k, sizeof k, "F%d", key - GLFW_KEY_F1 + 1);
    else if (key == GLFW_KEY_DELETE) snprintf(k, sizeof k, "Del");
    else if (key == GLFW_KEY_BACKSPACE) snprintf(k, sizeof k, "Backspace");
    else snprintf(k, sizeof k, "?");
    int c = mods & GLFW_MOD_CONTROL, s = mods & GLFW_MOD_SHIFT;
    if (c && s) snprintf(buf, (size_t)cap, "Ctrl+Shift+%s", k);
    else if (c) snprintf(buf, (size_t)cap, "Ctrl+%s", k);
    else if (s) snprintf(buf, (size_t)cap, "Shift+%s", k);
    else snprintf(buf, (size_t)cap, "%s", k);
    return buf;
}

// Все аккорды записи через " / " (пусто, если клавиш нет).
void ed_cmd_keys(int i, char *buf, int cap) {
    const EdCmd *a = ed_cmd_at(i);
    buf[0] = 0;
    if (!a) return;
    char b1[40], b2[40];
    if (a->key) {
        ed_cmd_key_name(a->key, a->mods, b1, sizeof b1);
        snprintf(buf, (size_t)cap, "%s", b1);
    }
    if (a->key2) {
        ed_cmd_key_name(a->key2, a->mods2, b2, sizeof b2);
        int used = (int)strlen(buf);
        if (used < cap) snprintf(buf + used, (size_t)(cap - used), "%s%s", a->key ? " / " : "", b2);
    }
}
