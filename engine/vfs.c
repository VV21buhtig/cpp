// VFS-состояние (единственное на процесс): корень проекта.
// Чтение байтов осталось инлайн в vfs.h; тут только маунт и резолв.
#include <stdio.h>
#include <string.h>
#include "vfs.h"

static char g_root[1024] = {0};

void vfs_mount(const char *root) {
    if (!root) root = "";
    strncpy(g_root, root, sizeof g_root - 1);
    g_root[sizeof g_root - 1] = 0;
}

static int probe(const char *p) {
    if (!p || !p[0]) return 0;
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int vfs_resolve(const char *name, char *out, int cap) {
    if (!name || !out || cap <= 0) return 0;
    // 1. буквально (абсолютный путь или от CWD).
    if (probe(name)) {
        strncpy(out, name, (size_t)cap - 1);
        out[cap - 1] = 0;
        return 1;
    }
    // 2. корень проекта.
    if (g_root[0]) {
        snprintf(out, (size_t)cap, "%s/%s", g_root, name);
        if (probe(out)) return 1;
    }
    // 3-4. сток движка (от корня репо / рядом с движком).
    snprintf(out, (size_t)cap, "engine/%s", name);
    if (probe(out)) return 1;
    snprintf(out, (size_t)cap, "assets/%s", name);
    if (probe(out)) return 1;
    out[0] = 0;
    return 0;
}
