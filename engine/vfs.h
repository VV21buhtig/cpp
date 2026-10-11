#ifndef ENGINE_VFS_H
#define ENGINE_VFS_H
// VFS: байты по имени. Сейчас — десктоп (fopen), шов под веб (fetch).
// Склад (asset.h) читает только через это — перенос на веб тронет
// одну функцию, а не все ядра.
#include <stddef.h>
#include <stdio.h>

// Прочитать весь файл в out (до cap). Возвращает байты или 0.
static inline size_t vfs_read(const char *path, void *out, size_t cap) {
    if (!path || !out || !cap) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(out, 1, cap, f);
    fclose(f);
    return n;
}

// Размер файла или 0.
static inline size_t vfs_size(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n > 0 ? (size_t)n : 0;
}
#endif
