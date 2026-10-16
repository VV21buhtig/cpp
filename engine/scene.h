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

// Этап Б: ECS-lite. SoA как остальное (параллельные массивы, не указатели).
// X-списки — один источник полей (§5.2): имена+оффсеты, ловят дрейф
// _Static_assert + scene_test (паттерн по оффсетам туда-обратно).
typedef struct ScMaterial {
    Vec3 col;        // rgb 0..1
    float a;         // альфа (прозрачность уже умеет blend)
    float rough;     // шероховатость 0..1 (дефолт 0.8)
    float metal;     // металличность 0..1 (дефолт 0)
} ScMaterial;
#define SC_MATERIAL_FIELDS(X) \
    X(float, col.x) \
    X(float, col.y) \
    X(float, col.z) \
    X(float, a) \
    X(float, rough) \
    X(float, metal)
_Static_assert(sizeof(ScMaterial) == 6 * sizeof(float),
               "ScMaterial drifted from SC_MATERIAL_FIELDS");

// Anchor: привязка сущности (0 free, 1 static/не двигать, 2 follow за целью).
// follow = Entity id (0 нет). Симуляции нет (данные; интегратор — g_move §9).
typedef struct ScAnchor {
    int mode;
    int follow; // Entity id (0 нет)
    Vec3 off;   // смещение от цели (follow)
} ScAnchor;
#define SC_ANCHOR_FIELDS(X) \
    X(int, mode) \
    X(int, follow) \
    X(float, off.x) \
    X(float, off.y) \
    X(float, off.z)
_Static_assert(sizeof(ScAnchor) == 2 * sizeof(int) + 3 * sizeof(float),
               "ScAnchor drifted from SC_ANCHOR_FIELDS");

// Physics: данные движения (интегратора НЕТ — позже, см. §9 g_move).
typedef struct ScPhysics {
    Vec3 vel;     // скорость (мировые ед/с)
    float mass;   // масса (>0)
    int dyn;      // 1 динамическое, 0 кинематика/статика
} ScPhysics;
#define SC_PHYSICS_FIELDS(X) \
    X(float, vel.x) \
    X(float, vel.y) \
    X(float, vel.z) \
    X(float, mass) \
    X(int, dyn)
_Static_assert(sizeof(ScPhysics) == 4 * sizeof(float) + sizeof(int),
               "ScPhysics drifted from SC_PHYSICS_FIELDS");

typedef struct ScWorld {
    int alive[SC_MAX];
    int parent[SC_MAX]; // индекс родителя или -1 (корень)
    int visible[SC_MAX];
    char name[SC_MAX][SC_NAME];
    ScTransform tr[SC_MAX];
    ScMaterial mat[SC_MAX];   // Этап Б
    ScAnchor anchor[SC_MAX];  // Этап Б
    ScPhysics phys[SC_MAX];   // Этап Б (данные, без симуляции)
    float *mesh[SC_MAX]; // меш-компонент: копия супа 9 float/верт (0 = нет)
    int meshN[SC_MAX];   // вершин в супе
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
// Этап Б: материал/якорь/физика get/set (мертвая: get нули, set игнор).
ScMaterial sc_mat_get(const ScWorld *w, Entity e);
void sc_mat_set(ScWorld *w, Entity e, const ScMaterial *m);
ScAnchor sc_anchor_get(const ScWorld *w, Entity e);
void sc_anchor_set(ScWorld *w, Entity e, const ScAnchor *a);
ScPhysics sc_phys_get(const ScWorld *w, Entity e);
void sc_phys_set(ScWorld *w, Entity e, const ScPhysics *p);
int sc_visible(const ScWorld *w, Entity e);
void sc_show(ScWorld *w, Entity e, int on);
// Жива ли. Имя ("" если нет). Переименование (копия, обрезается).
int sc_alive(const ScWorld *w, Entity e);
const char *sc_name(const ScWorld *w, Entity e);
void sc_rename(ScWorld *w, Entity e, const char *name);
// Меш-компонент: положить копию супа (9 float/верт, 1..1000000) или снять (verts=0).
// 1 ок, 0 отказ (нет сущности / плохой размер / нет памяти).
int sc_mesh_set(ScWorld *w, Entity e, const float *verts, int nverts);
// Суп сущности (0/cnt если нет). Указатель живёт пока сущность/мир.
const float *sc_mesh_get(const ScWorld *w, Entity e, int *nverts);
// Список живых (порядок создания). Возвращает число (<= cap).
int sc_list(const ScWorld *w, Entity *out, int cap);

#ifdef __cplusplus
}
#endif
#endif // ENGINE_SCENE_H
