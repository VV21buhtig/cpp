#ifndef ENGINE_SCENE_H
#define ENGINE_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

// Сцена движка: сущности с именами, родителями и трансформами.
// Нужно Hierarchy (дерево) и Inspector (ползунки) редактора.
// Без GPU и рендера: чистые данные + тесты. Поведение/компоненты — позже.
#include <stdint.h>
#include "vec.h"

#define SC_MAX 1024
#define SC_NAME 48

typedef uint32_t Entity; // 0 = нет такой (индекс+1, без поколений v1)

typedef struct ScTransform {
    Vec3 pos;    // позиция
    Vec3 rot;    // yaw/pitch/roll, радианы
    Vec3 scale;  // масштаб (1,1,1 = как есть)
} ScTransform;

typedef struct ScWorld {
    int alive[SC_MAX];
    int parent[SC_MAX]; // индекс родителя или -1 (корень)
    int visible[SC_MAX];
    char name[SC_MAX][SC_NAME];
    ScTransform tr[SC_MAX];
    int count; // живых
} ScWorld;

void sc_init(ScWorld *w);
// Создать сущность с именем (копия, обрезается). 0 если пул полон.
Entity sc_spawn(ScWorld *w, const char *name);
// Убить (дети осиротевают к корню, не каскадом — честно и просто).
void sc_kill(ScWorld *w, Entity e);
// Родитель (рекурсий не проверяем глубже прямого цикла — не стреляй в ногу).
// Возвращает 1 если встал.
int sc_parent(ScWorld *w, Entity e, Entity parent);
// Чей ребёнок (сущность родителя, 0 корень/нет). Для дерева иерархии.
Entity sc_parent_of(const ScWorld *w, Entity e);
// Трансформ get/set. set на мёртвой — игнор.
ScTransform sc_get(const ScWorld *w, Entity e);
void sc_set(ScWorld *w, Entity e, const ScTransform *t);
int sc_visible(const ScWorld *w, Entity e);
void sc_show(ScWorld *w, Entity e, int on);
// Жива ли. Имя ("" если нет). Переименование (копия, обрезается).
int sc_alive(const ScWorld *w, Entity e);
const char *sc_name(const ScWorld *w, Entity e);
void sc_rename(ScWorld *w, Entity e, const char *name);
// Список живых (порядок создания). Возвращает число (<= cap).
int sc_list(const ScWorld *w, Entity *out, int cap);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_SCENE_H
