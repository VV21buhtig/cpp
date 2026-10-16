// Проект и сцена в C-бекенде (единый источник правды для любого клиента:
// Qt-редактор, будущий Electron, headless-тесты). Никакого Qt здесь.
#ifndef ENGINE_PROJECT_H
#define ENGINE_PROJECT_H

#include "scene.h"
#include "graph.h" // EngGraph (граф хранится в project.db)

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
// Этап Б: одна сущность (тот же набор ключей). free(). 0 аргументы/сущности.
// Парсер: частичное обновление present-ключами, атомарно (битый JSON = 0,
// ничего не меняет). mass обязана быть >0, follow обязан жить.
char *eng_scene_entity_json(const ScWorld *w, Entity e);
int eng_scene_entity_parse(ScWorld *w, Entity e, const char *js);
// Этап Б2: project.db (SQLite). Хендл на каталог проекта (project.db внутри).
// entities — снимок мира (soft delete флагом), journal — транзакции + undo/redo
// (rev=1 откачено; новая мутация redo убивает). undo kill = респаун из снимка
// (id новый — честно); redo kill = по имени (ровно одно совпадение).
// Связка с EngView — следующим шагом; сейчас хранилище + тесты.
typedef struct EngDb EngDb;
EngDb *eng_db_open(const char *dir);
void eng_db_close(EngDb *db);
// Мир -> entities одной транзакцией (журнал не трогаем). 1 ок.
int eng_db_sync(EngDb *db, const ScWorld *w);
// entities (без deleted) -> мир (родители картой id). 1 ок.
int eng_db_load(EngDb *db, ScWorld *w);
// Мутация поля (имя из X-таблицы: pos.x..name/visible/parent) + журнал +
// зеркало в entities. 1 ок, 0 отказ (поле/значение/сущность).
int eng_db_set(EngDb *db, ScWorld *w, unsigned ent, const char *field, const char *value);
// Soft delete + журнал (снимок в before). 1 ок.
int eng_db_kill(EngDb *db, ScWorld *w, unsigned ent);
int eng_db_undo(EngDb *db, ScWorld *w); // 1 если было что откатить
int eng_db_redo(EngDb *db, ScWorld *w); // 1 если было что повторить
int eng_db_count(EngDb *db, int *nents, int *njournal); // счётчики для тестов
int eng_db_nfields(void); // число полей таблицы (тест сверяет с X-списками)
// Этап В: граф в тех же project.db (таблицы nodes/links, замена целиком).
// save чистит таблицы и пишет граф; load чистит граф и читает. 1 ок.
int eng_graph_save(EngDb *db, const EngGraph *gr);
int eng_graph_load(EngDb *db, EngGraph *gr);
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
