// Шовные типы ввода/привязки (по Slate, адаптация под C11, не буквально):
// - UiReply как FReply: handled + capture (захват мыши модальному виджету).
//   Цикл шлёт MOVE/UP захватившему, остальные потребители пропускают.
// - UiAttrF как TAttribute<float>: значение XOR геттер (живая привязка к ECS
//   вместо копий; копии врут при внешних правках — инспектор так умирал бы).
#ifndef ENGINE_UI_BIND_H
#define ENGINE_UI_BIND_H

typedef struct {
    int handled; // 1 съедено (дальше не маршрутизировать)
    int dirty; // 1 стало грязно (перестроить кадр)
    int capture; // 1 слать сюда MOVE/UP пока не отпустит (возврат capture=0)
} UiReply;

static inline UiReply ui_handled(int dirty, int capture) {
    UiReply r;
    r.handled = 1;
    r.dirty = dirty;
    r.capture = capture;
    return r;
}

static inline UiReply ui_unhandled(void) {
    UiReply r;
    r.handled = 0;
    r.dirty = 0;
    r.capture = 0;
    return r;
}

typedef float (*UiGetF)(void *user);
typedef void (*UiSetF)(void *user, float v);

// Привязка float: либо прямое value, либо живая (get/set обязательны оба).
typedef struct {
    int bound;
    float value;
    UiGetF get;
    UiSetF set;
    void *user;
} UiAttrF;

static inline float ui_attr_f_get(const UiAttrF *a) {
    if (!a) return 0.0f;
    if (a->bound && a->get) return a->get(a->user);
    return a->value;
}

static inline void ui_attr_f_set(UiAttrF *a, float v) {
    if (!a) return;
    if (a->bound) {
        if (a->set) a->set(a->user, v);
    } else {
        a->value = v;
    }
}

#endif // ENGINE_UI_BIND_H
