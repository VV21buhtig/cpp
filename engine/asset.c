// Склад ассетов: import-копия + FNV-дедуп + текстовый индекс. C11.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asset.h"
#include "vfs.h"

// FNV-1a 64: дёшево, без зависимостей. Коллизий на количествах ассетов нет.
static uint64_t fnv(const uint8_t *d, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void idx_path(const char *root, char *out, int cap) {
    snprintf(out, (size_t)cap, "%s/index.txt", root);
}

// Расширение исходника (после точки) или "bin".
static const char *ext_of(const char *path) {
    const char *dot = strrchr(path, '.');
    const char *sep = strrchr(path, '/');
    if (!dot || (sep && dot < sep) || !dot[1]) return "bin";
    return dot + 1;
}

AssetId asset_import(const char *root, const char *path) {
    if (!root || !path) return 0;
    size_t n = vfs_size(path);
    if (!n || n > (1u << 26)) return 0; // пусто/больше 64МБ — не ассет
    uint8_t *buf = (uint8_t *)malloc(n);
    if (!buf) return 0;
    if (vfs_read(path, buf, n) != n) { free(buf); return 0; }
    uint64_t h = fnv(buf, n);
    char dst[512], idx[512];
    snprintf(dst, sizeof dst, "%s/%016llx.%s", root, (unsigned long long)h, ext_of(path));
    // Дедуп: уже лежит — просто вернуть ID.
    {
        FILE *f = fopen(dst, "rb");
        if (f) { fclose(f); free(buf); return h; }
    }
    FILE *f = fopen(dst, "wb");
    if (!f) { free(buf); return 0; }
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    free(buf);
    if (w != n) return 0;
    // Индекс: дописать строку с именем В ДЕПО (исходник не важен).
    idx_path(root, idx, sizeof idx);
    f = fopen(idx, "a");
    if (f) {
        const char *dn = strrchr(dst, '/');
        fprintf(f, "%016llx %s %zu\n", (unsigned long long)h, dn ? dn + 1 : dst, n);
        fclose(f);
    }
    return h;
}

int asset_path(const char *root, AssetId id, char *out, int cap) {
    if (!root || !id || !out || cap <= 0) return 0;
    char idx[512];
    idx_path(root, idx, sizeof idx);
    FILE *f = fopen(idx, "rb");
    if (!f) return 0;
    // Строка: "<hex16> <имя-в-депо> <размер>".
    char want[24];
    snprintf(want, sizeof want, "%016llx ", (unsigned long long)id);
    char line[640];
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, want, 17) != 0) continue;
        char *nm = line + 17;
        char *sp = strrchr(nm, ' ');
        if (sp) *sp = 0; // отрезать размер
        char *nl = strchr(nm, '\n');
        if (nl) *nl = 0;
        if (!nm[0]) continue;
        snprintf(out, (size_t)cap, "%s/%s", root, nm);
        ok = 1;
        break;
    }
    fclose(f);
    if (!ok) return 0;
    FILE *probe = fopen(out, "rb");
    if (!probe) return 0;
    fclose(probe);
    return 1;
}

int asset_count(const char *root) {
    if (!root) return -1;
    char idx[512];
    idx_path(root, idx, sizeof idx);
    FILE *f = fopen(idx, "rb");
    if (!f) return -1;
    int c = 0;
    char line[640];
    while (fgets(line, sizeof line, f))
        if (line[0] != '\n' && line[0]) c++;
    fclose(f);
    return c;
}
