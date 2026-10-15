// Сканер theme.json: ищет "key": [a, b, c, d]. Строгий JSON не нужен.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ui_theme.h"

void ui_theme_dark(UiTheme *t) {
    if (!t) return;
    static const float bg[4] = {0.16f, 0.17f, 0.19f, 1.0f};
    static const float bar[4] = {0.23f, 0.24f, 0.27f, 1.0f};
    static const float sel[4] = {0.10f, 0.36f, 0.60f, 1.0f};
    static const float tool[4] = {0.85f, 0.50f, 0.10f, 1.0f};
    static const float txt[4] = {0.85f, 0.85f, 0.87f, 1.0f};
    static const float dim[4] = {0.55f, 0.55f, 0.58f, 1.0f};
    memcpy(t->bg, bg, sizeof bg);
    memcpy(t->bar, bar, sizeof bar);
    memcpy(t->sel, sel, sizeof sel);
    memcpy(t->tool, tool, sizeof tool);
    memcpy(t->txt, txt, sizeof txt);
    memcpy(t->dim, dim, sizeof dim);
}

static int grab(const char *s, const char *key, float *out) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *f = strstr(s, pat);
    if (!f) return 0;
    f = strchr(f, '[');
    if (!f) return 0;
    return sscanf(f, "[%f ,%f ,%f ,%f", &out[0], &out[1], &out[2], &out[3]) == 4;
}

static long g_mtime = 0;

int ui_theme_load(UiTheme *t, const char *path) {
    if (!t || !path) return 0;
    FILE *ff = fopen(path, "rb");
    if (!ff) return 0;
    fseek(ff, 0, SEEK_END);
    long sz = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) {
        fclose(ff);
        return 0;
    }
    char *s = (char *)malloc((size_t)sz + 1);
    if (!s) {
        fclose(ff);
        return 0;
    }
    if (fread(s, 1, (size_t)sz, ff) != (size_t)sz) {
        free(s);
        fclose(ff);
        return 0;
    }
    s[sz] = 0;
    fclose(ff);
    UiTheme nt;
    ui_theme_dark(&nt);
    grab(s, "bg", nt.bg);
    grab(s, "bar", nt.bar);
    grab(s, "sel", nt.sel);
    grab(s, "tool", nt.tool);
    grab(s, "txt", nt.txt);
    grab(s, "dim", nt.dim);
    free(s);
    *t = nt;
    struct stat st;
    if (stat(path, &st) == 0) g_mtime = (long)st.st_mtime;
    return 1;
}

int ui_theme_poll(UiTheme *t, const char *path) {
    struct stat st;
    if (!t || !path || stat(path, &st) != 0) return 0;
    if ((long)st.st_mtime == g_mtime) return 0;
    return ui_theme_load(t, path);
}
