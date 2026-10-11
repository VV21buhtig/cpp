#ifndef ENGINE_ASSET_H
#define ENGINE_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

// Склад ассетов (модель CapCut): всё скидываешь в одно место, достаёшь по ID.
// Import копирует файл в депо (`<root>/<hex16>.<ext>`), дубли по хешу
// содержимого не плодятся (тот же ID). Проекты хранят ID, не пути.
// Индекс — текстовый `index.txt` (id, имя, размер), читаемый глазами.
#include <stdint.h>

typedef uint64_t AssetId; // 0 = нет такого

// Втащить файл в депо. Возвращает ID (старый при дедупе) или 0.
AssetId asset_import(const char *root, const char *path);
// Путь лежащего файла по ID (out, cap). 1 ок, 0 нет.
int asset_path(const char *root, AssetId id, char *out, int cap);
// Сколько записей в индексе (для UI-списка). -1 если депо нет.
int asset_count(const char *root);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_ASSET_H
