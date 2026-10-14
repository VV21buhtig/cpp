#ifndef ENGINE_VFS_H
#define ENGINE_VFS_H

#ifdef __cplusplus
extern "C" {
#endif

// VFS: байты по имени. Сейчас — десктоп (fopen), шов под веб (fetch).
// Склад (asset.h) читает только через это — перенос на веб тронет
// одну функцию, а не все ядра.
// Плюс маунт проекта: vfs_mount(root) + vfs_resolve(имя) ищет
// literally -> root/имя -> engine/имя -> assets/имя (первое живое).
// Шейдеры-ядра грузятся только через resolve: файлы проекта
// перекрывают стоковые (ассеты вместо констант).
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

// Корень проекта (шейдеры/ассеты проекта перекрывают сток). "" = нет.
// resolve: буквально -> root/имя -> engine/имя -> assets/имя.
// Возвращает 1 и кладёт живой путь в out, 0 если нигде нет.
void vfs_mount(const char *root);
int vfs_resolve(const char *name, char *out, int cap);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_VFS_H
