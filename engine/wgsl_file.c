// WGSL из файлов (Этап 3.1): вместо baked sdf_wgsl.h.
// Порядок байт-в-байт как embed_wgsl.cmake: sdf/*.wgsl по именам + "\n";
// небо: 00_atmo + "\n" + модуль. Malloc только тут (init/reload, не кадр).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "wgsl_file.h"
#include "vfs.h"

static int cmp_str(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static char *read_file(const char *path, size_t *outN) {
    FILE *ff = fopen(path, "rb");
    if (!ff) return 0;
    fseek(ff, 0, SEEK_END);
    long sz = ftell(ff);
    fseek(ff, 0, SEEK_SET);
    if (sz <= 0 || sz > (1 << 20)) {
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
    fclose(ff);
    s[sz] = 0;
    if (outN) *outN = (size_t)sz;
    return s;
}

// Каталог шейдеров через VFS (проект перекрывает сток). 0 = нет каталога.
static int shader_dir(const char *sub, char *out, int cap) {
    char virt[128];
    snprintf(virt, sizeof virt, "shaders/%s", sub);
    return vfs_resolve(virt, out, cap);
}

char *wgsl_sdf(void) {
    char dir[1024];
    if (!shader_dir("sdf", dir, sizeof dir)) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    char names[64][128];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != 0 && n < 64) {
        size_t L = strlen(e->d_name);
        if (L < 6 || L > 100 || strcmp(e->d_name + L - 5, ".wgsl")) continue;
        snprintf(names[n], sizeof names[n], "%s", e->d_name);
        n++;
    }
    closedir(d);
    if (!n) return 0;
    qsort(names, (size_t)n, sizeof names[0], cmp_str);
    // Суммарная длина + "\n" за частью (как embed).
    size_t total = 1;
    for (int i = 0; i < n; i++) {
        char p[1152];
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        FILE *ff = fopen(p, "rb");
        if (!ff) return 0;
        fseek(ff, 0, SEEK_END);
        long sz = ftell(ff);
        fclose(ff);
        if (sz <= 0 || sz > (1 << 20)) return 0;
        total += (size_t)sz + 1;
    }
    char *out = (char *)malloc(total);
    if (!out) return 0;
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        char p[1152];
        snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        size_t rn = 0;
        char *c = read_file(p, &rn);
        if (!c) {
            free(out);
            return 0;
        }
        memcpy(out + pos, c, rn);
        pos += rn;
        out[pos++] = '\n';
        free(c);
    }
    out[pos] = 0;
    return out;
}

char *wgsl_sky(const char *which) {
    const char *mod = 0;
    if (!strcmp(which, "TRANS")) mod = "10_trans.wgsl";
    else if (!strcmp(which, "MS")) mod = "20_ms.wgsl";
    else if (!strcmp(which, "VIEW")) mod = "30_view.wgsl";
    else if (!strcmp(which, "AMB")) mod = "40_amb.wgsl";
    else return 0;
    char dir[1024];
    if (!shader_dir("sky", dir, sizeof dir)) return 0;
    char pa[1152], pb[1152];
    snprintf(pa, sizeof pa, "%s/00_atmo.wgsl", dir);
    snprintf(pb, sizeof pb, "%s/%s", dir, mod);
    size_t na = 0, nb = 0;
    char *a = read_file(pa, &na);
    char *b = read_file(pb, &nb);
    if (!a || !b) {
        free(a);
        free(b);
        return 0;
    }
    char *out = (char *)malloc(na + 1 + nb + 1);
    if (!out) {
        free(a);
        free(b);
        return 0;
    }
    memcpy(out, a, na);
    out[na] = '\n';
    memcpy(out + na + 1, b, nb);
    out[na + 1 + nb] = 0;
    free(a);
    free(b);
    return out;
}
