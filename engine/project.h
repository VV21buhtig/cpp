// Проект и сцена в C-бекенде (единый источник правды для любого клиента:
// Qt-редактор, будущий Electron, headless-тесты). Никакого Qt здесь.
#ifndef ENGINE_PROJECT_H
#define ENGINE_PROJECT_H

#include "scene.h"

#ifdef __cplusplus
extern "C" {
#endif

// scene.txt туда-обратно (dir = корень проекта). 1 ок, 0 отказ.
// save: e ИМЯ РДИТ ВИД XYZ XYZ XYZ (родители — индексами в файл).
// load: чистит мир, грузит, вяжет родителей вторым проходом.
// Нет файла — 0 (вызыватель решает: дефолты скелета или ошибка).
int eng_scene_save(const ScWorld *w, const char *dir);
int eng_scene_load(ScWorld *w, const char *dir);
// Снимок дерева для тонкого клиента (Electron): JSON, строка 0 = верх.
// Вызыватель free(). NULL если память не дали.
char *eng_scene_json(const ScWorld *w);
// Создание проекта: root/имя + project.txt + scene.txt по шаблону.
// tpl: "Empty" (Scene+Camera) или любой другой (Scene+Camera+Light+Mesh).
// backend 1/2/3 (остальное -> 2). err/errcap — текст отказа (может быть NULL).
// Возвращает 1 если создан, 0 если отказ (уже есть / плохое имя / диск).
int eng_project_create(const char *root, const char *name, const char *tpl, int backend,
                       char *err, int errcap);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_PROJECT_H
