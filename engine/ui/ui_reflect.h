// X-макросы рефлексии: ОДИН список полей -> дескрипторы + save/load +
// инспектор + undo. Без парсера, без сгенерированных файлов, C11.
// Правило: новое поле компонента = одна строка в списке. Голые таблицы,
// расходящиеся со структурой, запрещены (ловятся _Static_assert + selftest).
#ifndef ENGINE_UI_REFLECT_H
#define ENGINE_UI_REFLECT_H

#include <stddef.h>
#include "scene.h" // ScTransform (структуры НЕ переопределяем, только ссылаемся)

#define UI_F_FLOAT 0
#define UI_F_INT 1

typedef struct {
    const char *name; // "pos.x" — точка работает и в #name, и в offsetof
    int type; // UI_F_*
    size_t off; // offsetof от базы компонента
} UiField;

// Transform = существующий ScTransform{pos,rot,scale: Vec3}, 9 float.
// X(type, ctype, name): type для дескриптора/инспектора, остальное для кода.
#define TRANSFORM_FIELDS(X) \
    X(UI_F_FLOAT, float, pos.x) \
    X(UI_F_FLOAT, float, pos.y) \
    X(UI_F_FLOAT, float, pos.z) \
    X(UI_F_FLOAT, float, rot.x) \
    X(UI_F_FLOAT, float, rot.y) \
    X(UI_F_FLOAT, float, rot.z) \
    X(UI_F_FLOAT, float, scale.x) \
    X(UI_F_FLOAT, float, scale.y) \
    X(UI_F_FLOAT, float, scale.z)

// Раскрытие 1: дескрипторы (один массив на всех потребителей).
#define X(ftype, ctype, name) \
    { \
#name, ftype, offsetof(ScTransform, name) },
static const UiField transform_fields[] = {
    TRANSFORM_FIELDS(X)
};
#undef X
#define TRANSFORM_NFIELDS (int)(sizeof transform_fields / sizeof transform_fields[0])

// Раскрытие 0 (не массив, а проверка): список покрывает всю структуру.
// ScTransform = 3×Vec3 = 9 float подряд без дыр (Vec3{float×3}, padding нет).
_Static_assert(sizeof(ScTransform) == 9 * sizeof(float),
               "ScTransform layout drifted from TRANSFORM_FIELDS");

// Чтение/запись float-поля по дескриптору (инспектор/undo/save).
static inline float ui_field_get_f(const void *base, const UiField *f) {
    return *(const float *)((const char *)base + f->off);
}
static inline void ui_field_set_f(void *base, const UiField *f, float v) {
    *(float *)((char *)base + f->off) = v;
}

#endif // ENGINE_UI_REFLECT_H
